import math

lat = 53.195
long = 174.488


planetRadius = 931.3;
x = planetRadius * math.cos(math.radians(lat))*math.cos(math.radians(long))
y = planetRadius * math.cos(math.radians(lat))*math.sin(math.radians(long))
z = planetRadius * math.sin(math.radians(lat))

print(f"lat={lat}, long={long} → x={x:.2f}, y={y:.2f}, z={z:.2f}")