#pragma once

#include "scout_core.h"

#include <string>
#include <optional>
#include <limits>
#include <nlohmann/json.hpp>
#include <point_store_sqlite.h>

struct StarmapPoi {
	int item_id = 0;
	std::string System;
	std::string Planet;
	std::string PoiName;
	std::string Type;
	std::string Classification;
	double Latitude = std::numeric_limits<double>::quiet_NaN();
	double Longitude = std::numeric_limits<double>::quiet_NaN();
	double Longitude360 = std::numeric_limits<double>::quiet_NaN();
	double Height = std::numeric_limits<double>::quiet_NaN();
	double XCoord = std::numeric_limits<double>::quiet_NaN();
	double YCoord = std::numeric_limits<double>::quiet_NaN();
	double ZCoord = std::numeric_limits<double>::quiet_NaN();
	std::optional<int> QTMarker;
	std::string NextPOI;
	std::string NextQTMarker;
	std::string Description;
	std::string LastChange;
	std::string Introduced;
	std::string POI_Faction;
	std::string POI_Subtype;
	std::string POI_Entries;
	std::optional<int> POI_Accessable_Foot;
	std::optional<int> POI_Accessable_Vehicle;
	std::optional<int> POI_Accessable_Ship;
	std::string POI_Defenses;
	std::optional<std::string> POI_LandingPads;
	std::string POI_NPCs;
	std::optional<int> POI_Atmosphere;
	std::optional<int> ZONE_Armistice;
	std::optional<int> ZONE_NoFly;
	std::optional<int> ZONE_Trespassing;
	std::string ZONE_Biome;
	std::optional<int> ZONE_Gravitation;
	std::string ZONE_Temperature_Min;
	std::string ZONE_Temperature_Max;
	std::string GUID;
	std::string ImageLabels;
	nlohmann::json raw;
};

inline int json_parse_int(const nlohmann::json& j) {
	if (j.is_string()) {
		auto value = j.get<std::string>();
		if (value == "NaN")
			return 0;
		else
			return std::stoi(value);
	} else if (j.is_number_integer()) {
		return j.get<int>();
	} else {
		throw std::runtime_error("Expected int or string for integer field");
	}
};

inline void from_json(const nlohmann::json& j, StarmapPoi& p) {
	p.raw = j;
	p.item_id = j.value("item_id", 0);
	p.System = j.value("System", "");
	p.Planet = j.value("Planet", "");
	p.PoiName = j.value("PoiName", "");
	p.Type = j.value("Type", "");
	p.Classification = j.value("Classification", "");

	if (j.contains("Latitude") && !j["Latitude"].is_null()) p.Latitude = j["Latitude"].get<double>();
	if (j.contains("Longitude") && !j["Longitude"].is_null()) p.Longitude = j["Longitude"].get<double>();
	if (j.contains("Longitude360") && !j["Longitude360"].is_null()) p.Longitude360 = j["Longitude360"].get<double>();
	if (j.contains("Height") && !j["Height"].is_null()) p.Height = j["Height"].get<double>();
	if (j.contains("XCoord") && !j["XCoord"].is_null()) p.XCoord = j["XCoord"].get<double>();
	if (j.contains("YCoord") && !j["YCoord"].is_null()) p.YCoord = j["YCoord"].get<double>();
	if (j.contains("ZCoord") && !j["ZCoord"].is_null()) p.ZCoord = j["ZCoord"].get<double>();

	if (j.contains("QTMarker") && !j["QTMarker"].is_null()) p.QTMarker = j["QTMarker"].get<int>();
	if (j.contains("NextPOI") && !j["NextPOI"].is_null()) p.NextPOI = j["NextPOI"].get<std::string>();
	if (j.contains("NextQTMarker") && !j["NextQTMarker"].is_null()) p.NextQTMarker = j["NextQTMarker"].get<std::string>();
	if (j.contains("Description") && !j["Description"].is_null()) p.Description = j["Description"].get<std::string>();
	if (j.contains("LastChange") && !j["LastChange"].is_null()) p.LastChange = j["LastChange"].get<std::string>();
	if (j.contains("Introduced") && !j["Introduced"].is_null()) p.Introduced = j["Introduced"].get<std::string>();
	if (j.contains("POI_Faction") && !j["POI_Faction"].is_null()) p.POI_Faction = j["POI_Faction"].get<std::string>();
	if (j.contains("POI_Subtype") && !j["POI_Subtype"].is_null()) p.POI_Subtype = j["POI_Subtype"].get<std::string>();
	if (j.contains("POI_Entries") && !j["POI_Entries"].is_null()) p.POI_Entries = j["POI_Entries"].get<std::string>();

	if (j.contains("POI_Accessable_Foot") && !j["POI_Accessable_Foot"].is_null()) {
		p.POI_Accessable_Foot = json_parse_int(j["POI_Accessable_Foot"]);
	}
	if (j.contains("POI_Accessable_Vehicle") && !j["POI_Accessable_Vehicle"].is_null()) {
		p.POI_Accessable_Vehicle = json_parse_int(j["POI_Accessable_Vehicle"]);
	}
	if (j.contains("POI_Accessable_Ship") && !j["POI_Accessable_Ship"].is_null()) {
		p.POI_Accessable_Ship = json_parse_int(j["POI_Accessable_Ship"]);
	}

	if (j.contains("POI_Defenses") && !j["POI_Defenses"].is_null()) p.POI_Defenses = j["POI_Defenses"].get<std::string>();
	if (j.contains("POI_LandingPads") && !j["POI_LandingPads"].is_null()) { p.POI_LandingPads = j["POI_LandingPads"].get<std::string>(); }
	if (j.contains("POI_NPCs") && !j["POI_NPCs"].is_null()) p.POI_NPCs = j["POI_NPCs"].get<std::string>();
	if (j.contains("POI_Atmosphere") && !j["POI_Atmosphere"].is_null()) { p.POI_Atmosphere = json_parse_int(j["POI_Atmosphere"]); }

	if (j.contains("ZONE_Armistice") && !j["ZONE_Armistice"].is_null()) p.ZONE_Armistice = j["ZONE_Armistice"].get<int>();
	if (j.contains("ZONE_NoFly") && !j["ZONE_NoFly"].is_null()) p.ZONE_NoFly = j["ZONE_NoFly"].get<int>();
	if (j.contains("ZONE_Trespassing") && !j["ZONE_Trespassing"].is_null()) p.ZONE_Trespassing = j["ZONE_Trespassing"].get<int>();
	if (j.contains("ZONE_Biome") && !j["ZONE_Biome"].is_null()) p.ZONE_Biome = j["ZONE_Biome"].get<std::string>();
	if (j.contains("ZONE_Gravitation") && !j["ZONE_Gravitation"].is_null()) p.ZONE_Gravitation = j["ZONE_Gravitation"].get<int>();
	if (j.contains("ZONE_Temperature_Min") && !j["ZONE_Temperature_Min"].is_null()) p.ZONE_Temperature_Min = j["ZONE_Temperature_Min"].get<std::string>();
	if (j.contains("ZONE_Temperature_Max") && !j["ZONE_Temperature_Max"].is_null()) p.ZONE_Temperature_Max = j["ZONE_Temperature_Max"].get<std::string>();

	if (j.contains("GUID") && !j["GUID"].is_null())	p.GUID = j.value("GUID", "");
	else p.GUID = SqlitePointStore::generate_uuid_v4();
	if (j.contains("ImageLabels") && !j["ImageLabels"].is_null()) {
		if (j["ImageLabels"].is_string()) p.ImageLabels = j["ImageLabels"].get<std::string>();
		else p.ImageLabels = j["ImageLabels"].dump();
	}
}


//Type: Underground Facility
//Subtype : Security Bunker
//Subtype : Hurston Dynamics Operations Facility
//Subtype : Platform
//Type : Security Outpost
//Type : Cave
//Subtype : Sand Cave
//Subtype : Sink Hole
//Subtype : Sand Cave, Wreck, Derelict Outpost
//Subtype : Rock Cave
//Subtype : Rock
//Subtype : rock
//Type : Derelict Outpost
//Type : Outpost
//Subtype : Mining Outpost
//Type : Wreck
//Subtype : Ship Wreck
//Subtype : Caterpillar Puzzle Wreck
//Subtype : Sand Cave, Wreck, Derelict Outpost
//Type : Druglab
//Type : Easteregg
//Subtype : Easteregg
//Type : Animal Area
//Type : Event
//Type : Object Container
//Type : Orbital Station
//Subtype : Security Outpost
//Type : Landing Zone
//Type : Racetrack(Community)
//Subtype : Sand Cave, Wreck, Derelict Outpost
//Type : Racetrack
//Subtype : Community, SCR
//Type : River
//Type : Onyx Facility
//Type : Comm Array
//Type : Abandoned Outpost
//Type : Spaceport
//Type : Forward Operating Base
//Type : Scrapyard
//Type : Jump Point
//Type : Derelict Settlement
//Type : Planetary Alignment Facility
//Type : Prison
//Type : RestStop
//Type : Colonial Outpost
//Type : Missing Derelict Outpost
//Type : Unknown
//Type : Distribution Center
//Type : Mission Area
//Type : Colonial Bunker
//Type : Asteroid Base
//Type : Orbital Laser Platform
//Subtype : Orbital Laser Platform
//Type : Ground Activation Platform
//Subtype : Ground Activation Platform
//Type : Asteroid Belt
//Type : Station
//Type : LandingZone

inline PoiType get_PoiType(const StarmapPoi& poi) {
	auto type_str = poi.Type.empty() ? "Unknown" : poi.Type;

	if (type_str.empty()) {
		return PoiType::Unknown;
	}
	if (type_str == "Cave" || type_str == "Rock Cave" || type_str == "Sand Cave" || type_str == "Sink Hole") {
		return PoiType::Cave;
	}
	auto locationtypes = { "Druglab",
		"Easteregg", 
		"Animal Area", 
		"Event", 
		"Object Container", 
		"River", 
		"Mission Area", 
		"RestStop", 
		"Distribution Center",
		"Derelict Outpost", 
		"Outpost",
		"Abandoned Outpost",
		"Underground Facility",
		"Colonial Bunker"
	};
	// 
	auto it = std::find(locationtypes.begin(), locationtypes.end(), type_str);
	if (it != locationtypes.end()) {
		return PoiType::Location;
	}
	return PoiType::Unknown;
}

inline DataPoint starmap_poi_to_datapoint(const StarmapPoi& poi) {
	DataPoint p;
	p.id = 0; // to be set by store
	p.server = "All"; // not applicable
	p.planet = poi.Planet;
	p.x = poi.XCoord;
	p.y = poi.YCoord;
	p.z = poi.ZCoord;
	p.poi_type = get_PoiType(poi);
	p.material = poi.POI_Subtype.empty() ? poi.Type : (poi.Type + "/" + poi.POI_Subtype);
	p.quality_max = 0; // not applicable
	p.note = poi.PoiName;
	return p;
}
