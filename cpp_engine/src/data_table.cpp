#include "data_table.h"
#include "scout_app.h"
#include "scout_core.h"
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <memory>

DataTable::DataTable() {
	init();
}

void DataTable::init() {
	filters.clear();
	filters.resize(15);
	sort_column = "id";
	sort_order = 2;
	page_size = 100;
	page_index = 0;
	index_map.clear();
	prev_filters.clear();
	prev_sort_col.clear();
	prev_sort_order = -1;
	prev_points_size = 0;
	change_list.clear();
	data_dirty = false;
	save_message.clear();
	// no per-cell label caches when using PushID
}

const int asc{ 1 };
const int desc{ 2 };

// Helper: match a datapoint against current filters
static bool matches_filters(const DataPoint& p, const std::vector<std::string>& filters, const std::vector<const char *>& materials) {
	// Column 0: id
	if (!filters[0].empty()) {
		if (std::to_string(p.id).find(filters[0]) == std::string::npos) return false;
	}
	if (!filters[1].empty()) {
		if (p.time_info.find(filters[1]) == std::string::npos) return false;
	}
	if (!filters[2].empty()) {
		if (p.server.find(filters[2]) == std::string::npos) return false;
	}
	if (!filters[3].empty()) { if (std::to_string(p.coord.x).find(filters[3]) == std::string::npos) return false; }
	if (!filters[4].empty()) { if (std::to_string(p.coord.y).find(filters[4]) == std::string::npos) return false; }
	if (!filters[5].empty()) { if (std::to_string(p.coord.z).find(filters[5]) == std::string::npos) return false; }
	if (!filters[6].empty()) {
		if (p.planet.find(filters[6]) == std::string::npos) return false;
	}
	if (!filters[7].empty()) { if (std::string(poi_type_name(p.poi_type)).find(filters[7]) == std::string::npos) return false; }
	if (!filters[8].empty()) { if (std::string(poi_subtype_name(p.subtype)).find(filters[8]) == std::string::npos) return false; }
	if (!filters[9].empty()) { if (p.material.find(filters[9]) == std::string::npos) return false; }
	if (!filters[10].empty()) { if (std::to_string(p.quality_min).find(filters[10]) == std::string::npos) return false; }
	if (!filters[11].empty()) { if (std::to_string(p.quality_max).find(filters[11]) == std::string::npos) return false; }
	if (!filters[12].empty()) { if (p.note.find(filters[12]) == std::string::npos) return false; }
	if (!filters[13].empty()) { if (std::string(p.qt_persistent ? "1" : "0").find(filters[13]) == std::string::npos) return false; }
	return true;
}

void DataTable::render(AppState& state) {
	ImGui::Begin("Datatable", &state.data_form_active);
			// Editable table for DataPoint entries
	static bool data_dirty = false;
	static std::string save_message;

	ImGui::Text("Edit existing points (changes are in-memory until you Save)");
	ImGui::Separator();

	// Prepare datatablepoints by applying header filters and sorting
	if (filters.size() < 15) filters.resize(15);



	if (state.planets.size() != planets.size()) {
					planets.clear();
					planets.reserve(state.planets.size());
					for (const auto& s : state.planets) planets.push_back(s.c_str());
				}
				if (poi_types.size() != poi_impl::poi_type_count) {
					poi_types.reserve(poi_impl::poi_type_count);
					for (size_t si = 0; si < poi_impl::poi_type_count; ++si) poi_types.push_back(poi_impl::poi_type_names_arr[si]);
				}
				if (poi_subtypes.size() != poi_impl::poi_subtype_count) {
					poi_subtypes.reserve(poi_impl::poi_subtype_count);
					for (size_t si = 0; si < poi_impl::poi_subtype_count; ++si) poi_subtypes.push_back(poi_impl::poi_subtype_names_arr[si]);
				}
				if (materials.size() != state.materials.size()) {
					materials.clear();
					materials.reserve(state.materials.size());
					for (const auto& s : state.materials) materials.push_back(s.c_str());
				}


	// detect changes
	if (prev_filters.size() != filters.size()) prev_filters.resize(filters.size());
	bool filters_changed = false;
	for (size_t i = 0; i < filters.size(); ++i) {
		if (prev_filters[i] != filters[i]) { filters_changed = true; break; }
	}
	bool sort_changed = (prev_sort_col != sort_column) || (prev_sort_order != sort_order);
	bool points_changed = (prev_points_size != state.points.size()); //|| (data_dirty != prev_data_dirty);

	if (filters_changed || sort_changed || points_changed) {

		datatable_index_map.clear();
		datatable_index_map.reserve(state.points.size());
		for (size_t i = 0; i < state.points.size(); ++i) {
			if (matches_filters(state.points[i], filters, materials)) {
				datatable_index_map.push_back(i);
			}
		}
		

		// Sorting index map based on selected column
		if (!sort_column.empty() && sort_order != 0) {
			bool asc = (sort_order == 1);
			const std::string& c = sort_column;
			std::stable_sort(datatable_index_map.begin(), datatable_index_map.end(), [&](size_t ai, size_t bi) {
				const DataPoint& a = state.points[ai];
				const DataPoint& b = state.points[bi];
				if (c == "id") return asc ? a.id < b.id : a.id > b.id;
				if (c == "time") return asc ? a.time_info < b.time_info : a.time_info > b.time_info;
				if (c == "server") return asc ? a.server < b.server : a.server > b.server;
				if (c == "x") return asc ? a.coord.x < b.coord.x : a.coord.x > b.coord.x;
				if (c == "y") return asc ? a.coord.y < b.coord.y : a.coord.y > b.coord.y;
				if (c == "z") return asc ? a.coord.z < b.coord.z : a.coord.z > b.coord.z;
				if (c == "planet") return asc ? a.planet < b.planet : a.planet > b.planet;
				if (c == "material") return asc ? a.material < b.material : a.material > b.material;
				if (c == "qmin") return asc ? a.quality_min < b.quality_min : a.quality_min > b.quality_min;
				if (c == "qmax") return asc ? a.quality_max < b.quality_max : a.quality_max > b.quality_max;
				return asc ? a.id < b.id : a.id > b.id;
			});
		} else {
			// default sort by id ascending
			std::stable_sort(datatable_index_map.begin(), datatable_index_map.end(), [&](size_t ai, size_t bi) { return state.points[ai].id < state.points[bi].id; });
		}

		// update cached state
		prev_filters = filters;
		prev_sort_col = sort_column;
		prev_sort_order = sort_order;
		prev_points_size = state.points.size();
		prev_data_dirty = data_dirty;
	}

	size_t total_items = datatable_index_map.size();
	size_t total_pages = page_size > 0 ? (total_items + static_cast<size_t>(page_size) - 1) / static_cast<size_t>(page_size) : 1;
	if (page_size <= 0) page_size = 1;

	ImGui::PushID("datatable_paging");
	ImGui::Text("Page size:"); ImGui::SameLine();
	if (ImGui::InputInt("##page_size", &page_size)) {
		if (page_size <= 0) page_size = 1;
		total_pages = (total_items + static_cast<size_t>(page_size) - 1) / static_cast<size_t>(page_size);
		if (page_index >= static_cast<int>(total_pages) && total_pages > 0) page_index = static_cast<int>(total_pages) - 1;
	}
	ImGui::SameLine();
	if (ImGui::Button("Prev")) {
		if (page_index > 0) --page_index;
	}
	ImGui::SameLine();
	if (ImGui::Button("Next")) {
		if (page_index < static_cast<int>(std::max<size_t>(1, total_pages)) - 1) ++page_index;
	}
	ImGui::SameLine();
	// Page jump input (1-based)
	int page_input = page_index + 1;
	ImGui::PushItemWidth(80);
	if (ImGui::InputInt("##page_input", &page_input)) {
		if (page_input < 1) page_input = 1;
		if (total_pages > 0 && static_cast<size_t>(page_input) > total_pages) page_input = static_cast<int>(total_pages);
		page_index = page_input - 1;
	}
	ImGui::PopItemWidth();
	ImGui::SameLine();
	ImGui::Text("/ %zu", total_pages == 0 ? 1 : total_pages);
	ImGui::PopID();

	// helper lambdas for marking changes
	auto mark_modified = [&](int id) {
		if (id <= 0) return;
		for (const auto& c : change_list) {
			if (c.second == id && c.first == "delete") return; // deleted takes precedence
		}
		for (const auto& c : change_list) {
			if (c.second == id && c.first == "modify") return; // already marked
		}
		change_list.emplace_back("modify", id);
		};
	auto mark_deleted = [&](int id) {
		if (id <= 0) return;
		// remove any pending modify entries for this id
		change_list.erase(std::remove_if(change_list.begin(), change_list.end(), [&](const std::pair<std::string, int>& p) { return p.second == id && p.first == "modify"; }), change_list.end());
		for (const auto& c : change_list) {
			if (c.second == id && c.first == "delete") return; // already marked
		}
		change_list.emplace_back("delete", id);
		};

	ImGuiTableFlags table_flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY;
	// Use a distinct table ID to avoid legacy table-layout mismatches across builds
	if (ImGui::BeginTable("DataPointsTable_v3", 15, table_flags, ImVec2(0, ImGui::GetContentRegionAvail().y - 30))) {
		ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 30.0f); // 0
		ImGui::TableSetupColumn("Record Time", ImGuiTableColumnFlags_WidthFixed, 100.0f); // 1
		ImGui::TableSetupColumn("Server"); // 2
		ImGui::TableSetupColumn("X"); // 3
		ImGui::TableSetupColumn("Y"); // 4
		ImGui::TableSetupColumn("Z"); // 5
		ImGui::TableSetupColumn("Zone"); // 6
		ImGui::TableSetupColumn("POI Type", ImGuiTableColumnFlags_WidthFixed, 100.0f); // 7
		ImGui::TableSetupColumn("Subtype", ImGuiTableColumnFlags_WidthFixed, 160.0f); // 8
		ImGui::TableSetupColumn("Resource"); // 9
		ImGui::TableSetupColumn("QMin"); // 10
		ImGui::TableSetupColumn("QMax"); // 11
		ImGui::TableSetupColumn("Note"); // 12
		ImGui::TableSetupColumn("QT Persist", ImGuiTableColumnFlags_WidthFixed, 80.0f); // 13
		ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthFixed, 80.0f); //14
		ImGui::TableSetupScrollFreeze(0, 2);
		ImGui::TableHeadersRow();

		// Second header row: filter inputs and clickable sort buttons
		// Style the filter row to visually separate it from data rows and make inputs compact
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
		ImGui::TableNextRow();
		for (int col = 0; col < 15; ++col) {
			ImGui::TableSetColumnIndex(col);
			// Determine display name and key
			if (col == 14) continue;
			const char* disp = "";
			std::string key;
			switch (col) {
				case 0: disp = "ID"; key = "id"; break;
				case 1: disp = "Record Time"; key = "time"; break;
				case 2: disp = "Server"; key = "server"; break;
				case 3: disp = "X"; key = "x"; break;
				case 4: disp = "Y"; key = "y"; break;
				case 5: disp = "Z"; key = "z"; break;
				case 6: disp = "Zone"; key = "zone"; break;
				case 7: disp = "POI Type"; key = "poi_type"; break;
				case 8: disp = "Subtype"; key = "subtype"; break;
				case 9: disp = "Resource"; key = "material"; break;
				case 10: disp = "QMin"; key = "qmin"; break;
				case 11: disp = "QMax"; key = "qmax"; break;
				case 12: disp = "Note"; key = "note"; break;
				case 13: disp = "QT"; key = "qt"; break;
				case 14: disp = "Control"; key = "control"; break;
			}
			// show sort symbol if active (use ASCII arrows for compatibility)
			std::string btn_label = disp;
			if (sort_column == key) {
				if (sort_order == 1) btn_label += " ^";
				else if (sort_order == 2) btn_label += " v";
			}
			std::string btn_id = btn_label + std::string("##hdr") + std::to_string(col);
			// color active sort button green
			bool sort_key_changed = false;
			bool sort_to_none = false;
			if (sort_column == key && sort_order != 0) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.6f, 0.0f, 1.0f));
			if (ImGui::Button(btn_id.c_str())) {
				if (sort_column == key) {
					// cycle none -> asc -> desc -> none
					sort_order = (sort_order + 1) % 3;
					if (sort_order == 0) {
						sort_column.clear();
						sort_to_none = true;
					}
				} else {
					sort_key_changed = true;
					sort_column = key;
					sort_order = 1;
				}
			}
			if ((sort_column == key && !sort_key_changed && sort_order != 0) || sort_to_none) {
				ImGui::PopStyleColor();
			}
			// filter input below button
			// Compact per-column widths for filter inputs
			int iw = 100;
			switch (col) {
				case 0: iw = 60; break; // ID
				case 1: iw = 160; break; // Record Time
				case 2: iw = 100; break; // Server
				case 3: case 4: case 5: iw = 80; break; // X,Y,Z
				case 6: iw = 120; break; // Planet
				case 7: case 8: iw = 100; break; // POI Type, Subtype
				case 9: iw = 120; break; // Resource
				case 10: case 11: iw = 80; break; // QMin/QMax
				case 12: iw = 160; break; // Note
				case 13: iw = 80; break; // QT
				case 14: iw = 80; break; // Control
				default: iw = 100; break;
			}
			ImGui::PushItemWidth(static_cast<float>(iw));
			std::string f_lbl = std::string("##filter") + std::to_string(col);
			// For POI Type, Subtype and Resource use dropdowns
			if (col == 1) {
			} else if (col == 7) {
				auto names = poi_type_names();
				std::vector<const char*> cstrs;
				cstrs.reserve(names.size() + 1);
				cstrs.push_back("Any");
				for (const auto& s : names) cstrs.push_back(s.c_str());
				int sel = 0; // 0 == Any
				if (filters.size() > 7 && !filters[7].empty()) {
					for (size_t si = 0; si < names.size(); ++si) if (names[si] == filters[7]) { sel = static_cast<int>(si + 1); break; }
				}
				if (ImGui::Combo(f_lbl.c_str(), &sel, cstrs.data(), static_cast<int>(cstrs.size()))) {
					if (sel == 0) filters[7].clear(); else filters[7] = names[sel - 1];
				}
			} else if (col == 8) {
				std::vector<const char*> cstrs;
				cstrs.reserve(poi_impl::poi_subtype_count + 1);
				cstrs.push_back("Any");
				for (size_t si = 0; si < poi_impl::poi_subtype_count; ++si) cstrs.push_back(poi_impl::poi_subtype_names_arr[si]);
				int sel = 0;
				if (filters.size() > 8 && !filters[8].empty()) {
					for (size_t si = 0; si < poi_impl::poi_subtype_count; ++si) if (filters[8] == poi_impl::poi_subtype_names_arr[si]) { sel = static_cast<int>(si + 1); break; }
				}
				if (ImGui::Combo(f_lbl.c_str(), &sel, cstrs.data(), static_cast<int>(cstrs.size()))) {
					if (sel == 0) filters[8].clear(); else filters[8] = poi_impl::poi_subtype_names_arr[sel - 1];
				}
			} else if (col == 9) {
				std::vector<const char*> cstrs;
				cstrs.reserve(state.materials.size() + 1);
				cstrs.push_back("Any");
				for (const auto& s : state.materials) cstrs.push_back(s.c_str());
				int sel = 0;
				if (filters.size() > 9 && !filters[9].empty()) {
					for (size_t si = 0; si < state.materials.size(); ++si) if (filters[9] == state.materials[si]) { sel = static_cast<int>(si + 1); break; }
				}
				if (ImGui::Combo(f_lbl.c_str(), &sel, cstrs.data(), static_cast<int>(cstrs.size()))) {
					if (sel == 0) filters[9].clear(); else filters[9] = state.materials[sel - 1];
				}
			} else {
				char fbuf[128] = { 0 };
				if (filters.size() > static_cast<size_t>(col)) {
					strncpy(fbuf, filters[col].c_str(), sizeof(fbuf) - 1);
				}
				if (ImGui::InputText(f_lbl.c_str(), fbuf, sizeof(fbuf))) {
					filters[col] = fbuf;
				}
			}
			ImGui::PopItemWidth();
		}
		ImGui::PopStyleColor(2);

		std::vector<size_t> to_erase;

		size_t start = static_cast<size_t>(page_index) * static_cast<size_t>(page_size);
		if (start >= datatable_index_map.size()) start = 0;
		size_t end_idx = ((start + static_cast<size_t>(page_size)) < datatable_index_map.size()) ? (start + static_cast<size_t>(page_size)) : datatable_index_map.size();
		size_t control_element_id = 0;
		size_t idx = 0;
		for (size_t local = start; local < end_idx; ++local, ++idx) {
			size_t idx = datatable_index_map[local];
			DataPoint& dp = state.points[idx];
			ImGui::TableNextRow();
			ImGui::PushID(static_cast<int>(dp.id));

			// ID (read-only)
			ImGui::TableSetColumnIndex(0);
			ImGui::TextDisabled("%d", dp.id);

			// Record Time (read-only, formatted from time_info)
			ImGui::TableSetColumnIndex(1);
			if (dp.time_info.empty()) {
				ImGui::TextDisabled("N/A");
			} else {
				ImGui::Text("%s", dp.time_info.c_str());
			}

			// Server
			ImGui::TableSetColumnIndex(2);
			{
				char buf[16] = { 0 };
				strncpy(buf, dp.server.c_str(), sizeof(buf) - 1);
				ImGui::PushItemWidth(-FLT_MIN);
				if (ImGui::InputText("server", buf, IM_ARRAYSIZE(buf))) {
					dp.server = buf;
					data_dirty = true;
					mark_modified(dp.id);
				}
				ImGui::PopItemWidth();
			}

			// X
			ImGui::TableSetColumnIndex(3);
			{
				double val = dp.coord.x;
				ImGui::PushItemWidth(-FLT_MIN);
				if (ImGui::InputDouble("x", &val, 0.0, 0.0, "%.6f")) {
					dp.coord.x = val;
					data_dirty = true;
					mark_modified(dp.id);
				}
				ImGui::PopItemWidth();
			}

			// Y
			ImGui::TableSetColumnIndex(4);
			{
				double val = dp.coord.y;
				ImGui::PushItemWidth(-FLT_MIN);
				if (ImGui::InputDouble("y", &val, 0.0, 0.0, "%.6f")) {
					dp.coord.y = val;
					data_dirty = true;
					mark_modified(dp.id);
				}
				ImGui::PopItemWidth();
			}

			// Z
			ImGui::TableSetColumnIndex(5);
			{
				double val = dp.coord.z;
				ImGui::PushItemWidth(-FLT_MIN);
				if (ImGui::InputDouble("z", &val, 0.0, 0.0, "%.6f")) {
					dp.coord.z = val;
					data_dirty = true;
					mark_modified(dp.id);
				}
				ImGui::PopItemWidth();
			}

			// Planet (combo)
			ImGui::TableSetColumnIndex(6);
			{
				auto it = std::find(planets.begin(), planets.end(), dp.planet);
				int cur = it != planets.end() ? static_cast<int>(std::distance(planets.begin(), it)) : 0;
				ImGui::PushItemWidth(-FLT_MIN);
				if (ImGui::Combo("planet", &cur, planets.data(), static_cast<int>(planets.size()))) {
					dp.planet = planets[static_cast<size_t>(cur)];
					data_dirty = true;
					mark_modified(dp.id);
				}
				ImGui::PopItemWidth();
			}

			// POI Type (enum-backed combo)
			ImGui::TableSetColumnIndex(7);
			{
				int cur = static_cast<int>(dp.poi_type);
				ImGui::PushItemWidth(-FLT_MIN);
				if (ImGui::Combo("poi_type", &cur, poi_types.data(), static_cast<int>(poi_types.size()))) {
					dp.poi_type = static_cast<PoiType>(cur);
					data_dirty = true;
					mark_modified(dp.id);
				}
				ImGui::PopItemWidth();
			}

			// Subtype (enum-backed combo)
			ImGui::TableSetColumnIndex(8);
			{
				int cur = static_cast<int>(dp.subtype);
				ImGui::PushItemWidth(-FLT_MIN);
				if (ImGui::Combo("subtype", &cur, poi_subtypes.data(), static_cast<int>(poi_subtypes.size()))) {
					dp.subtype = static_cast<PoiSubType>(cur);
					data_dirty = true;
					mark_modified(dp.id);
				}
				ImGui::PopItemWidth();
			}

			// Resource (combo)
			ImGui::TableSetColumnIndex(9);
			{
				auto it = std::find(state.materials.begin(), state.materials.end(), dp.material);
				int cur = it != state.materials.end() ? static_cast<int>(std::distance(state.materials.begin(), it)) : 0;
				ImGui::PushItemWidth(-FLT_MIN);
				if (ImGui::Combo("material", &cur, materials.data(), static_cast<int>(materials.size()))) {
					dp.material = state.materials[static_cast<size_t>(cur)];
					data_dirty = true;
					mark_modified(dp.id);
				}
				ImGui::PopItemWidth();
			}

			// QMin
			ImGui::TableSetColumnIndex(10);
			{
				int val = int(dp.quality_min);
				ImGui::PushItemWidth(-FLT_MIN);
				if (ImGui::InputInt("qmin", &val, 0, 0)) {
					dp.quality_min = val;
					data_dirty = true;
					mark_modified(dp.id);
				}
				ImGui::PopItemWidth();
			}

			// QMax
			ImGui::TableSetColumnIndex(11);
			{
				int val = int(dp.quality_max);
				ImGui::PushItemWidth(-FLT_MIN);
				if (ImGui::InputInt("qmax", &val, 0, 0)) {
					dp.quality_max = val;
					data_dirty = true;
					mark_modified(dp.id);
				}
				ImGui::PopItemWidth();
			}

			// Note
			ImGui::TableSetColumnIndex(12);
			{
				char buf[256] = { 0 };
				strncpy(buf, dp.note.c_str(), sizeof(buf) - 1);
				ImGui::PushItemWidth(-FLT_MIN);
				if (ImGui::InputText("note", buf, IM_ARRAYSIZE(buf))) {
					dp.note = buf;
					data_dirty = true;
					mark_modified(dp.id);
				}
				ImGui::PopItemWidth();
			}

			// QT persistent checkbox
			ImGui::TableSetColumnIndex(13);
			{
				bool val = dp.qt_persistent;
				if (ImGui::Checkbox("##qt_persistent", &val)) {
					dp.qt_persistent = val;
					data_dirty = true;
					mark_modified(dp.id);
				}
			}

			// Controls (Delete)
			ImGui::TableSetColumnIndex(14);
			{
				if (ImGui::SmallButton("Delete")) {
					mark_deleted(dp.id);
					to_erase.push_back(idx);
				}
			}

			ImGui::PopID();
		}

		// erase rows in reverse order
		if (!to_erase.empty()) {
			std::sort(to_erase.rbegin(), to_erase.rend());
			for (size_t idx : to_erase) {
				if (idx < state.points.size()) {
					state.points.erase(state.points.begin() + static_cast<std::ptrdiff_t>(idx));
					data_dirty = true;
				}
			}

			// Rebuild datatable index map after removals
			//datatable_index_map.clear();
			//for (size_t i = 0; i < state.points.size(); ++i) {
			//	if (matches(state.points[i])) datatable_index_map.push_back(i);
			//}
		}

		// After potential removals, ensure the current page index is valid
		total_items = datatable_index_map.size();
		total_pages = page_size > 0 ? (total_items + static_cast<size_t>(page_size) - 1) / static_cast<size_t>(page_size) : 1;
		if (total_pages == 0) {
			page_index = 0;
		} else if (page_index >= static_cast<int>(total_pages)) {
			page_index = static_cast<int>(total_pages) - 1;
		}
		// no per-cell label cache to trim when using PushID per-row

		ImGui::EndTable();
	}

	


	ImGui::Separator();
	bool save_enabled = !change_list.empty();
	if (!save_enabled) ImGui::BeginDisabled();
	if (ImGui::Button("Save Changes")) {
		bool ok = true;
		if (state.store) {
			// If we have a store and a change list, apply incremental changes where possible
			if (!change_list.empty()) {
				// Try to apply per-change operations against SqliteStore when available
				for (const auto& c : change_list) {
					const std::string& op = c.first;
					int rid = c.second;
					if (op == "delete") {
						if (!state.store->delete_point_by_id(rid)) {
							ok = false;
						}
					} else if (op == "modify") {
						// locate datapoint in memory
						auto it = std::find_if(state.points.begin(), state.points.end(), [&](const DataPoint& p) { return p.id == rid; });
						if (it != state.points.end()) {
							int res = state.store->uuid_insert_or_update(*it, nullptr);
							if (res == 0) ok = false;
						} else {
							// record not present locally anymore; nothing to update
						}
					}
				}
			}
			// } else {
			// 	// no per-record changes tracked -> fallback to full overwrite
			// 	ok = state.store->overwrite_points(state.points);
			// }
		} else {
			ok = false;
		}

		if (ok) {
			save_message = "Saved successfully";
			// Clear tracked changes and reload
			change_list.clear();
			state.reload_planet_data();
			state.filter_points();
			data_dirty = false;
		} else {
			save_message = "Save failed";
		}
	}
	if (!save_enabled) ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Reload Data")) {
		state.reload_planet_data();
		state.filter_points();
		save_message = "Reloaded";
		data_dirty = false;
	}
	ImGui::SameLine();
	ImGui::Text("%s", save_message.c_str());
	ImGui::End();
}
