#pragma once

#include "point_store.h"
#include <string>

class CsvPointStore : public IPointStore {
public:
    explicit CsvPointStore(const std::string& csv_path);
    ~CsvPointStore() override = default;

    std::vector<DataPoint> load_points() override;
    bool append_point(const DataPoint& p, std::string* out_change_id = nullptr) override;
    bool overwrite_points(const std::vector<DataPoint>& points) override;
    bool push_change_event(const ChangeEvent& ev) override;

private:
    std::string csv_path_;
};
