# read data.json array and convert to  
import json
import csv
data = None
with open("data.json", "r") as f:
    data = json.load(f)

planet = "Pyro_Pyro4"
if data is not None:
    
    # write to csv file data.csv with columns x , y , z , planet , _ , _ , note 
    with open("data.csv", "w", newline='') as csvfile:
        writer = csv.writer(csvfile)
        writer.writerow(["x", "y", "z", "planet", "quality_min", "quality_max", "note"])
        for item in data:
            x, y, z = item["xyz"]
            note = item["name"] if "name" in item else ""
            writer.writerow([x, y, z, planet, "", "", note])