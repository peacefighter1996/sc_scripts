#%%
# !pip install tensorflow opencv-python scikit-learn matplotlib 
# !pip install --upgrade scikit-learn
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



def build_model(num_classes):
    model = Sequential([ 
        Conv2D(32, (3,3), activation='relu', input_shape=(14,9,1)), 
        MaxPooling2D((2,2)), 
        Conv2D(32, (3,3), activation='relu'), 
        Flatten(), 
        Dense(64, activation='relu'), 
        Dense(64, activation='relu'), 
        Dense(num_classes, activation='softmax') 
        ])

    optimizer = tf.keras.optimizers.AdamW(learning_rate=1e-3, weight_decay=1e-4)
    model.compile(
        optimizer=optimizer,
        loss="sparse_categorical_crossentropy",
        metrics=['accuracy']
    )
    

    return model

X, y, label_map = load_dataset("dataset")

# save label map to file
import json
with open("label_map.json", "w") as f:
    json.dump(label_map, f)

X, y = augment_noise(X, y, copies=5)
X, y = augment_darkening(X, y, copies=5)
X = preprocess(X)
X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.3, shuffle=True)
model = build_model(len(label_map))

model.fit(X_train, y_train, epochs=10, batch_size=32, validation_data=(X_test, y_test))

model.save("char_recognition_model.keras")

def predict_char(model, img, label_map):
    img = cv2.resize(img, (9,12))
    img = img / 255.0
    img = img[np.newaxis, ..., np.newaxis]

    pred = model.predict(img)
    idx = np.argmax(pred)

    inv_map = {v:k for k,v in label_map.items()}
    return inv_map[idx]



## create desision tree for comparison
from sklearn.tree import DecisionTreeClassifier
# split set into train and test
X_train_flat = X_train.reshape(X_train.shape[0], -1)
X_test_flat = X_test.reshape(X_test.shape[0], -1)
dt = DecisionTreeClassifier()
dt.fit(X_train_flat, y_train)



## build confusion matrix for CNN model and decision tree model
from sklearn.metrics import confusion_matrix, ConfusionMatrixDisplay    
import matplotlib.pyplot as plt
y_pred = model.predict(X_test)
y_pred_labels = np.argmax(y_pred, axis=1)
cm_nn = confusion_matrix(y_test, y_pred_labels)
disp = ConfusionMatrixDisplay(confusion_matrix=cm_nn, display_labels=label_map.keys())
disp.plot(cmap=plt.cm.Blues)
plt.title("CNN Confusion Matrix")
plt.show()
y_pred_dt = dt.predict(X_test_flat)
cm_dt = confusion_matrix(y_test, y_pred_dt)
disp_dt = ConfusionMatrixDisplay(confusion_matrix=cm_dt, display_labels=label_map.keys())
disp_dt.plot(cmap=plt.cm.Blues)
plt.title("Decision Tree Confusion Matrix")
plt.show()

# show diffrences in in confussion matrix by plotting the difference between the two confusion matrices
cm_diff = cm_nn - cm_dt
disp_diff = ConfusionMatrixDisplay(confusion_matrix=cm_diff, display_labels=label_map.keys())
disp_diff.plot(cmap=plt.cm.RdBu)    
plt.title("Difference in Confusion Matrices (CNN - Decision Tree)")
plt.show()

# calculate overall accuracy diffrence between the two models
accuracy_nn = np.trace(cm_nn) / np.sum(cm_nn)
accuracy_dt = np.trace(cm_dt) / np.sum(cm_dt)
print(f"CNN Accuracy: {accuracy_nn:.4f}")
print(f"Decision Tree Accuracy: {accuracy_dt:.4f}")


## visualize decision tree
from sklearn.tree import plot_tree
plt.figure(figsize=(20,10))
plot_tree(dt, filled=True, feature_names=[f"pixel_{i}" for i in range(X_train_flat.shape[1])], class_names=list(label_map.keys()))
plt.title("Decision Tree Visualization (max depth=3)")
plt.show()

