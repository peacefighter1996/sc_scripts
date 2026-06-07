#pragma once

#include "scout_core.h"

#include <string>
#include <optional>
#include <limits>
#include <nlohmann/json.hpp>
#include <point_store_sqlite.h>
#include "uuid.h"
#include <algorithm>
#include <iostream>

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
	else p.GUID = uuid::generate_uuid_v4().to_string();
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

inline std::pair<PoiType, PoiSubType> get_PoiType(const StarmapPoi& poi) {
	auto type_str = poi.Type.empty() ? "Unknown" : poi.Type;

	if (type_str.empty()) {
		return {PoiType::Unknown, PoiSubType::None};
	}
	if (type_str == "Cave") {
		auto subtype_str = poi.POI_Subtype.empty() ? "None" : poi.POI_Subtype;
		PoiSubType subtype;
		if (!poi_subtype_from_string(subtype_str, subtype)) {
			subtype = PoiSubType::None;
		}
		if (subtype == PoiSubType::None){
			if (poi.PoiName.find("Sand Cave") != std::string::npos) subtype = PoiSubType::Sand_Cave;
			else if (poi.PoiName.find("Rock Cave") != std::string::npos || poi.PoiName.find("RockCave") != std::string::npos) subtype = PoiSubType::Rock_Cave;
			else if (poi.PoiName.find("Sink Hole") != std::string::npos) subtype = PoiSubType::Sink_Hole;
			else if (poi.PoiName.find("Acidic Cave") != std::string::npos || poi.PoiName.find("Acidic") != std::string::npos) subtype = PoiSubType::Acidic_Cave;
		}

		return {PoiType::Cave, subtype};
	}
	auto locationtypes = { 
		"Underground Facility",
		"Security Outpost",
		"Derelict Outpost",
		"Outpost",
		"Druglab",
		"Easteregg",
		"Animal Area",
		"Event",
		"Object Container",
		"Orbital Station",
		"Landing Zone",
		"Racetrack(Community)",
		"Racetrack",
		"Onyx Facility",
		"Comm Array",
		"Abandoned Outpost",
		"Spaceport",
		"Forward Operating Base",
		"Scrapyard",
		"Jump Point",
		"Derelict Settlement",
		"Planetary Alignment Facility",
		"Prison",
		"RestStop",
		"Colonial Outpost",
		"Missing Derelict Outpost",
		"Distribution Center",
		"Mission Area",
		"Colonial Bunker",
		"Asteroid Base",
		"Orbital Laser Platform",
		"Ground Activation Platform",
		"Asteroid Belt",
		"Station",
		"LandingZone"
		
	};
	

	auto it = std::find(locationtypes.begin(), locationtypes.end(), type_str);
	if (it != locationtypes.end()) {
		auto subtype_str = poi.POI_Subtype.empty() ? poi.Type : poi.POI_Subtype;
		PoiSubType subtype;
		if (!poi_subtype_from_string(subtype_str, subtype)) {
			subtype = PoiSubType::None;
		}
		return {PoiType::Location, subtype};
	}

	auto wreck_types = {
		"Wreck"
	};
	it = std::find(wreck_types.begin(), wreck_types.end(), type_str);
	if (it != wreck_types.end()) {
		auto subtype_str = poi.POI_Subtype.empty() ? "None" : poi.POI_Subtype;
		PoiSubType subtype;
		if (!poi_subtype_from_string(subtype_str, subtype)) {
			subtype = PoiSubType::None;
		}

		if (subtype == PoiSubType::None){
			if (poi.Classification.find("Derelict Crash Site") != std::string::npos) subtype = PoiSubType::Ship_Wreck;
			else if (poi.PoiName.find("Ship Wreck") != std::string::npos || poi.PoiName.find("Shipwreck") != std::string::npos || poi.Classification.find("Ship") != std::string::npos|| poi.Classification.find("ship") != std::string::npos) subtype = PoiSubType::Ship_Wreck;
			else if (to_lower(poi.PoiName).find("satellite") != std::string::npos || to_lower(poi.PoiName).find("satelite") != std::string::npos) subtype = PoiSubType::Satellite_Wreck;
			else if (poi.PoiName.find("Caterpillar Puzzle Wreck") != std::string::npos) subtype = PoiSubType::Caterpillar_Puzzle_Wreck;
		}
		return {PoiType::Wreck, subtype};
	}

	if (poi.PoiName.find("Mining Tower") != std::string::npos) {
		return {PoiType::Location, PoiSubType::Mining_Tower};
	}

	auto other_types = {
		"River"
	};

	it = std::find(other_types.begin(), other_types.end(), type_str);
	if (it != other_types.end()) {
		return {PoiType::Other, PoiSubType::River};
	}

	// add Lazarus Transport Hub based on name 
	if (poi.PoiName.find("Lazarus Transport Hub") != std::string::npos) {
		return {PoiType::Location, PoiSubType::Lazarus_Transport_Hub};
	}

	return {PoiType::Unknown, PoiSubType::None};
}

inline bool starmap_poi_to_datapoint(const StarmapPoi& poi, DataPoint& p) {
	p.id = poi.item_id; // to be set by store
	p.server = "All"; // not applicable
	p.planet = poi.Planet;
	p.coord.x = poi.XCoord;
	p.coord.y = poi.YCoord;
	p.coord.z = poi.ZCoord;
	auto [type, subtype] = get_PoiType(poi);
	p.poi_type = type;
	p.subtype = subtype;
	if (poi.GUID.empty()) {
		std::cerr << "Warning : POI " << poi.PoiName << ". Skipping due to missing GUID, and no fallback UUID could be generated.\n";
		return false;
	} else {
		try {
			if (poi.GUID.find('_') != std::string::npos) {
				// Handle split and use first part as UUID if multiple GUIDs are present separated by underscores
				auto parts = poi.GUID.substr(0, poi.GUID.find('_'));
				if (!parts.empty()) {
					p.uuid = uuid::from_string(parts);
				}
				else {
					std::cerr << "Warning : POI " << poi.PoiName << ". Skipping due to missing GUID, and no fallback UUID could be generated.\n";
					return false;
				}
			}
			else {
				p.uuid = uuid::from_string(poi.GUID);
			}
		}
		catch (std::invalid_argument& e) {
			std::cerr << "Warning : POI " << poi.PoiName << ". Skipping due to missing GUID, and no fallback UUID could be generated.\n";
			return false;
		}
	}
	p.qt_persistent = poi.QTMarker.has_value() && poi.QTMarker == 1;
	p.quality_max = 0; // not applicable
	p.note = poi.PoiName; //+ " | Type: " + poi.Type + (poi.POI_Subtype.empty() ? "" : (", Subtype: " + poi.POI_Subtype)) + " | Description: " + poi.Description;
	return true;
}
