#include "point_store_csv.h"
#include "scout_core.h"

// write_points_csv is implemented in scout_app.cpp; forward-declare it so we can call it here.
extern bool write_points_csv(const std::string& csv_path, const std::vector<DataPoint>& points);

CsvPointStore::CsvPointStore(const std::string& csv_path)
    : csv_path_(csv_path) {}

std::vector<DataPoint> CsvPointStore::load_points() {
    return ::load_points(csv_path_);
}

bool CsvPointStore::append_point(const DataPoint& p, std::string* out_change_id) {
    // CSV backend doesn't produce change_ids; optional out_change_id left untouched
    return ::append_point(csv_path_, p);
}

bool CsvPointStore::overwrite_points(const std::vector<DataPoint>& points) {
    return write_points_csv(csv_path_, points);
}

bool CsvPointStore::push_change_event(const ChangeEvent& /*ev*/) {
    // No-op for CSV backend; events are not tracked here
    return true;
}
