# read data.json array and convert to  
import json
import csv
data = None
with open("data.json", "r") as f:
    data = json.load(f)

if data is not None:
    
    # write to csv file data.csv with columns x , y , z , planet , _ , _ , note 
    with open("data.csv", "w", newline='') as csvfile:
        writer = csv.writer(csvfile)
        writer.writerow(["x", "y", "z", "planet", "quality_min", "quality_max", "note"])
        for planet in data:
            planet_name = planet["planet"]
            for record in planet["records"]:
                x, y, z = record["xyz"]
                x= x/1000
                y= y/1000
                z= z/1000
                note = record["name"] if "name" in record else ""
                writer.writerow([x, y, z, planet_name, "", "", note])