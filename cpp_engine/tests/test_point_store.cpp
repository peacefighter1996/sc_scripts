#include <iostream>
#include <cmath>
#include "point_store_sqlite.h"
#include "scout_core.h"

int main() {
    SqliteStore store(":memory:", "testnode");
    if (!store.init()) {
        std::cerr << "store.init() failed\n";
        return 1;
    }

    DataPoint p{};
    p.id = 1;
    p.server = "unittest";
    p.x = 1.234;
    p.y = 5.678;
    p.z = 9.1011;
    p.planet = "TestPlanet";
    p.material = "Iron";
    p.location = true;
    p.quality_min = 10;
    p.quality_max = 50;
    p.note = "unit test";
    p.poi_type = PoiType::Mineral;
    p.subtype = PoiSubType::None;
    p.qt_persistent = false;

    uuid change_id;
    if (!store.append_point(p, &change_id)) {
        std::cerr << "append_point failed\n";
        return 2;
    }

    auto points = store.load_points();
    if (points.size() != 1u) {
        std::cerr << "expected 1 point, got " << points.size() << "\n";
        return 3;
    }
    if (points[0].id != p.id) { std::cerr << "id mismatch\n"; return 4; }
    if (points[0].server != p.server) { std::cerr << "server mismatch\n"; return 5; }
    if (std::fabs(points[0].x - p.x) > 1e-9) { std::cerr << "x mismatch\n"; return 6; }
    if (points[0].uuid == nil_uuid) { std::cerr << "uuid not set\n"; return 7; }

    std::cout << "OK\n";
    return 0;
}
