#%%
!pip install tensorflow opencv-python scikit-learn matplotlib 
!pip install --upgrade scikit-learn
#%%
import os
import cv2
import numpy as np
from sklearn.model_selection import train_test_split
from tensorflow.keras.models import Sequential
from tensorflow.keras.layers import Conv2D, MaxPooling2D, Flatten, Dense
from tensorflow.keras.callbacks import EarlyStopping
import matplotlib.pyplot as plt
import numpy as np
import json

import tensorflow as tf

def load_dataset(path, size=(14, 9)):
    X = []
    y = []
    labels = sorted(os.listdir(path))
    label_map = {l: i for i, l in enumerate(labels)}

    for label in labels:
        folder = os.path.join(path, label)
        for file in os.listdir(folder):
            img = cv2.imread(os.path.join(folder, file), cv2.IMREAD_GRAYSCALE)
            if img is None:
                continue

            img = cv2.resize(img, (size[1], size[0]))
            X.append(img)
            y.append(label_map[label])

    X = np.array(X, dtype=np.float32)
    y = np.array(y)

    return X, y, label_map

def augment_noise(X, y, copies=3):
    X_aug = []
    y_aug = []

    for img, label in zip(X, y):
        X_aug.append(img)
        y_aug.append(label)

        for _ in range(copies):
            noise = np.random.randint(-10, 11, img.shape)
            noisy = img + noise

            noisy = np.clip(noisy, 0, 255)
            X_aug.append(noisy)
            y_aug.append(label)

    return np.array(X_aug), np.array(y_aug)


def augment_darkening(X, y, copies=3):
    X_aug = []
    y_aug = []

    for img, label in zip(X, y):
        X_aug.append(img)
        y_aug.append(label)

        for _ in range(copies):
            factor = np.random.uniform(0.7, 0.9)
            darkened = img * factor

            darkened = np.clip(darkened, 0, 255)
            X_aug.append(darkened)
            y_aug.append(label)

    return np.array(X_aug), np.array(y_aug)

def preprocess(X):
    X = X / 255.0
    X = X[..., np.newaxis]  # (N, 12, 8, 1)
    return X



def build_model(genome, input_shape, num_classes):
    i = 0
    model = Sequential()
    
    model.add(tf.keras.layers.Input(shape=input_shape))
    # ---- Conv part ----
    n_conv = genome[i]; i += 1

    for layer_idx in range(n_conv):
        k_w = genome[i]; k_h = genome[i+1]
        filters = genome[i+2]
        maxpool = genome[i+3]
        i += 4
        
        if k_w > 1 and k_h > 1 and filters > 0:
            model.add(Conv2D(filters, (k_w, k_h),
                                activation='relu'))

        if maxpool:
            model.add(MaxPooling2D((2,2)))
        
            
    i = 3*4 + 1  # reset index to after conv layers

    # ---- Transition ----
    
    if len(genome) < 21:
        genome += ['flatten']  # ensure flatten exists for dense part
        model.add(Flatten())
    elif genome[20] == 'flatten':
        model.add(Flatten())
    elif genome[20] == 'global':
        model.add(tf.keras.layers.GlobalAveragePooling2D())

    # ---- Dense part ----
    n_dense = genome[i]; i += 1
    # print(f"Genome: {genome} -> n_conv={n_conv}, n_dense={n_dense}")

    for _ in range(n_dense):
        units = genome[i]
        activation = genome[i+1]
        i += 2

        model.add(Dense(units, activation=activation))

    # ---- Output ----
    model.add(Dense(num_classes, activation='softmax'))
    
    # print model summary
    # model.summary()

    return genome, model

def evaluate_model(model, val_input, val_labels):
    pred = model.predict(val_input, verbose=0)
    ccm = tf.math.confusion_matrix(val_labels, np.argmax(pred, axis=1))
    acc = np.trace(ccm) / np.sum(ccm)    
    
    conv_params = 0
    dense_params = 0

    for layer in model.layers:
        if "conv" in layer.name:
            conv_params += layer.count_params()
        elif "dense" in layer.name:
            dense_params += layer.count_params()

    return (conv_params, dense_params, acc)

def dominates(a, b):
    return (
        a[0] <= b[0] and  # conv params (minimize)
        a[1] <= b[1] and  # dense params (minimize)
        a[2] >= b[2] and  # accuracy (maximize)
        (a[0] < b[0] or a[1] < b[1] or a[2] > b[2])  # at least one strictly better
    )
    
def pareto_front(population_scores):
    front = []

    for i, (score_i, genome_i) in enumerate(population_scores):
        dominated_flag = False

        for j, (score_j, _) in enumerate(population_scores):
            if j != i and dominates(score_j, score_i):
                dominated_flag = True
                break

        if not dominated_flag:
            front.append((score_i, genome_i))

    return front

import random

def mutate(genome):
    g = genome.copy()

    # mutate number of conv layers
    if random.random() < 0.2:
        g[0] = max(0, min(3, g[0] + random.choice([-1,1])))

    i = 1

    # mutate conv blocks
    for _ in range(3):
        if random.random() < 0.3:
            g[i] = random.choice([1,2,3,4])        # k_w
        if random.random() < 0.3:
            g[i+1] = random.choice([1,2,3,4])      # k_h
        if random.random() < 0.3:
            g[i+2] += int(random.uniform(-1, 1))  # filters
            if g[i+2] < 0:
                g[i+2] += 8
        if random.random() < 0.2:
            g[i+3] = 1 - g[i+3]                # maxpool flip

        i += 4
        

    # mutate dense layers
    if random.random() < 0.2:
        g[i] = max(0, min(3, g[i] + random.choice([-1,1])))

    i += 1

    for _ in range(3):
        if random.random() < 0.3:
            g[i] = int(random.uniform(-1, 1))  # units
            if g[i] < 1:
                g[i] += 8
        if random.random() < 0.1:
            g[i+1] = random.choice(['relu','tanh'])

        i += 2
        
    if len(g) < 21:
        g += ['flatten']  # ensure flatten exists for dense part
    
    if random.random() < 0.1:
        g[20] = random.choice(['flatten', 'global'])  # transition layer change

    return g

def random_genome():
    genome = [random.choice([0,1,2,3])]  # start with no conv layers

    for _ in range(3):  # max 3 conv layers
        genome += [random.choice([2,3,4]), random.choice([2,3,4]), random.randint(8, 64), random.choice([0,1])]

    genome += [random.choice([0,1,2,3])]  # start with no dense layers

    for _ in range(3):  # max 3 dense layers
        genome += [random.randint(8, 128), random.choice(['relu','tanh'])]

    return genome


def plot_pareto_3d_log(scored_population, pareto_front):
    fig = plt.figure()
    ax = fig.add_subplot(111, projection='3d')

    # Extract values
    conv = np.array([s[0][0] for s in scored_population])
    dense = np.array([s[0][1] for s in scored_population])
    acc = np.array([(1-s[0][2]) for s in scored_population])

    # Avoid log(0)
    conv = np.log10(conv + 1)
    dense = np.log10(dense + 1)
    acc = np.log10(acc + 1e-6)  # log of accuracy, add small value to avoid log(0)

    # Plot all individuals
    ax.scatter(conv, dense, acc)

    # Pareto front
    p_conv = np.array([s[0][0] for s in pareto_front])
    p_dense = np.array([s[0][1] for s in pareto_front])
    p_acc = np.array([(1-s[0][2]) for s in pareto_front])

    p_conv = np.log10(p_conv + 1)
    p_dense = np.log10(p_dense + 1)
    p_acc = np.log10(p_acc+1e-6)  # log of accuracy, add small value to avoid log(0)

    ax.scatter(p_conv, p_dense, p_acc, marker='x', s=50)

    # Labels
    ax.set_xlabel("log10(Conv Params)")
    ax.set_ylabel("log10(Dense Params)")
    ax.set_zlabel("log10(Error)")

    plt.title("3D Pareto Front (Log Scaled)")
    plt.show()
    
def plot_pareto_2d(scored_population, pareto_front):
    # ---- full population ----
    conv = np.array([s[0][0] for s in scored_population])
    dense = np.array([s[0][1] for s in scored_population])
    acc = np.array([1-s[0][2] for s in scored_population])

    conv_log = np.log10(conv + 1)
    dense_log = np.log10(dense + 1)

    plt.figure()

    scatter = plt.scatter(
        conv_log,
        dense_log,
        c=acc,
        alpha=0.6
    )

    # ---- pareto front ----
    p_conv = np.array([s[0][0] for s in pareto_front])
    p_dense = np.array([s[0][1] for s in pareto_front])
    p_acc = np.array([1-s[0][2] for s in pareto_front])

    p_conv_log = np.log10(p_conv + 1)
    p_dense_log = np.log10(p_dense + 1)

    # ---- sort for line drawing ----
    sort_idx = np.argsort(p_conv_log)
    p_conv_log = p_conv_log[sort_idx]
    p_dense_log = p_dense_log[sort_idx]

    # ---- pareto line ----
    plt.plot(
        p_conv_log,
        p_dense_log,
        linestyle='-',
        linewidth=2,
        color='black',
        label='Pareto Front'
    )

    # ---- pareto points ----
    # plt.scatter(
    #     p_conv_log,
    #     p_dense_log,
    #     s=10,
    #     color='red'
    # )

    plt.xlabel("log10(Conv Params)")
    plt.ylabel("log10(Dense Params)")
    plt.title("Pareto Front with Trade-off Line")

    plt.colorbar(scatter, label="Error (1 - Accuracy)")
    plt.legend()

    plt.show()

def get_model_signature(model):
    signature = []

    for layer in model.layers:
        config = layer.get_config()

        layer_type = layer.__class__.__name__

        if layer_type == "Conv2D":
            signature.append((
                "Conv2D",
                tuple(config["kernel_size"]),
                config["filters"],
                config["strides"],
                config["padding"]
            ))

        elif layer_type == "MaxPooling2D":
            signature.append(("MaxPool", tuple(config["pool_size"])))

        elif layer_type == "Flatten":
            signature.append(("Flatten",))
        elif layer_type == "GlobalAveragePooling2D":
            signature.append(("GlobalAvgPool",))

        elif layer_type == "Dense":
            signature.append((
                "Dense",
                config["units"],
                config["activation"]
            ))

    return tuple(signature)

# %%
X, y, label_map = load_dataset("../dataset/ocr_displayinfo")

# save label map to file
with open("label_map.json", "w") as f:
    json.dump(label_map, f)

X, y = augment_noise(X, y, copies=5)
X, y = augment_darkening(X, y, copies=5)
X = preprocess(X)
X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.3, shuffle=True)


num_classes = len(label_map)
population = [random_genome() for _ in range(20)]

# check if variable scored exists in current session, if not create it as empty list
if "scored" not in globals():
    scored = []
    model_cache = {}
if "front" not in globals():
    front = []

best_acc = 0.0

# import front from file if exists
if os.path.exists("../data/pareto_front_models.json"):
    with open("../data/pareto_front_models.json", "r") as f:
        saved_models = json.load(f)
        for item in saved_models:
            genome = item["genome"]
            score = tuple(item["score"])
            if tuple(genome) in model_cache:
                continue
            model_cache[tuple(genome)] = score
            scored.append((score, genome))
        front = pareto_front(scored)

for score, genome in scored:
    if score[2] > best_acc:
        best_acc = score[2]
        print(f"Best accuracy so far: {best_acc:.4f} with genome: {genome}")


for gen in range(100):

    for genome in population:
        # print(f"Evaluating genome: {genome}")
        try:
            genome, model = build_model(genome, (14,9,1), num_classes)
            
            signature = get_model_signature(model)
            if signature in model_cache:
                print("Using cached score for genome:", genome)
                scored.append((model_cache[signature], genome))
                continue
            
            if len(front) > 0:
                # check if genome would be dominated by any in current front if acc is 1.0 
                
                pre_evo = evaluate_model(model, X_test, y_test)
                pre_evo_score = (pre_evo[0], pre_evo[1], min(best_acc + 1e-4, 1.0))
                dominated = False
                for f_score, _ in front:
                    if dominates(f_score, pre_evo_score):
                        dominated = True
                        break
                if dominated:
                    print("Skipping evaluation for genome likely to be dominated:", genome)
                    continue
            
            
            optimizer = tf.keras.optimizers.AdamW(learning_rate=1e-3, weight_decay=1e-4)
            model.compile(optimizer=optimizer,
                          loss='sparse_categorical_crossentropy',
                          metrics=['accuracy'])
            print(f"Build model for genome: {genome}")
        except Exception as e:
            print(f"Error building model for genome {genome}: {e}")
            continue
        
        try:
            early_stop = EarlyStopping(
                monitor='val_accuracy',
                patience=2,
                restore_best_weights=True
            )

            model.fit(
                X_train, y_train,
                validation_data=(X_test, y_test),
                epochs=15,
                batch_size=32,
                callbacks=[early_stop],
                verbose=0
            )
        except Exception as e:
            print(f"Error training model for genome: {e}")
            continue
        try:
            score = evaluate_model(model, X_test, y_test)
            print(f"Score: {score}")
            model_cache[signature] = score
            scored.append((score, genome))
        except Exception as e:
            print(f"Error evaluating model for genome: {e}")
            continue

    front = pareto_front(scored)
    
    plot_pareto_3d_log(scored, front)
    plot_pareto_2d(scored, front)

    print(f"Gen {gen}: Pareto size = {len(front)}")

    # selection: keep 10 from front and 10 random from rest of population, then create new population by crossover and mutation
    r_front = random.sample(front, min(5, len(front)))
    parents = [genome for _, genome in r_front]
    # select 10 random genoms from population 
    parents += random.sample(population, 10)
    new_population = []

    while len(new_population) < 20:
        parent1, parent2 = random.sample(parents, 2)
        # random crossover 1 point
        point = random.randint(1, len(parent1)-1)
        child1 = parent1[:point] + parent2[point:]
        child2 = parent2[:point] + parent1[point:]
        child1 = mutate(child1)
        child2 = mutate(child2)        
        new_population.append(child1)
        new_population.append(child2)

    population = new_population

# %%

# remove duplicate genomes that produce the same Phenotype (conv params, dense params, accuracy) 
def FenoTypePruning(scored_population):
    scored_population.sort(key=lambda x: x[0])  # sort by score
    
    pruned = []
    result = []
    signatures = set()

    for score, genome in scored_population:
        model = build_model(genome, (14,9,1), num_classes)
            
        signature = get_model_signature(model) # get Phenotype signature of the model
        if signature in signatures:
            pruned.append((score, genome))
            continue
        signatures.add(signature)
        result.append((score, genome))
        

    return result, pruned



scored, pruned = FenoTypePruning(scored)

front = pareto_front(scored)
print(f"Gen Pareto size after pruning = {len(front)}")
plot_pareto_3d_log(scored, front)
plot_pareto_2d(scored, front)  
    
# %%
# save final pareto front models to disk as json files with their genome and score
import json
models = []
for score, genome in front:
    models.append({
        "genome": genome,
        "score": score
    })
    
# check if data folder exists, if not create it
if not os.path.exists("../data"):
    os.makedirs("../data")
with open("../data/pareto_front_models.json", "w") as f:
    json.dump(models, f, indent=4)
    
# %%
# get best model from pareto front, highest accuracy and lowest total params, and save it to disk
best_model = None
best_score = None
best_genome = None
for score, genome in front:
    if best_score is None or (score[2] > best_score[2]) or (score[2] == best_score[2] and score[0]+score[1] < best_score[0]+best_score[1]):
        best_score = score
        best_genome = genome

genome, best_model = build_model(best_genome, (14,9,1), num_classes)
best_model.compile(optimizer=tf.keras.optimizers.AdamW(learning_rate=1e-3, weight_decay=1e-4),
                   loss='sparse_categorical_crossentropy',
                   metrics=['accuracy'])
best_model.fit(X_train, y_train, validation_data=(X_test, y_test), epochs=15, batch_size=32, verbose=1)

# recheck accuracy of best model
final_score = evaluate_model(best_model, X_test, y_test)
print(f"Best Genome: {best_genome}")
print(f"Best Score: {final_score}")
best_model.save("data/best_pareto_model.keras")
# %%
