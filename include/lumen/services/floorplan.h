// Lumen — floor-plan overlay document (SPEC §10.4). Pure model + validation.
//
// The uploaded SVG is stored untouched (/lfs/floorplans/<id>.svg); everything
// Lumen adds lives in this overlay (config/floorplans/<id>.json):
//   {"schema_version":1,"id":"ground","name":"Ground floor",
//    "viewBox":[x,y,w,h],"hasSvg":true,
//    "lights":[{"lightId":"ceiling","x":120.5,"y":80,"radius":40,"color":"#ffcc88","icon":"bulb"}],
//    "rooms":[{"roomId":"living","points":[[0,0],[200,0],[200,150],[0,150]]}]}
// Positions are SVG viewBox coordinates, shared by the viewer and the editor.
#pragma once

#include <string>
#include <vector>

#include "core/cJSON.h"

namespace lumen::floorplan
{

constexpr int kSchemaVersion = 1;
constexpr std::size_t kMaxPlans = 3;
constexpr std::size_t kMaxLights = 64;
constexpr std::size_t kMaxRooms = 32;
constexpr std::size_t kMaxPoints = 32;

struct LightPin
{
    std::string light_id;
    double x = 0, y = 0, radius = 0;
    std::string color; // "" = from the light's live colour
    std::string icon;
};

struct RoomRegion
{
    std::string room_id;
    std::vector<std::pair<double, double>> points;
};

struct Plan
{
    std::string id;
    std::string name;
    double vb[4] = {0, 0, 1000, 1000};
    bool has_svg = false;
    std::vector<LightPin> lights;
    std::vector<RoomRegion> rooms;
};

bool valid_id(const std::string &id);

// Parses a stored overlay (lenient about unknown keys).
bool from_json(const cJSON *o, Plan &out, std::string &error);
cJSON *to_json(const Plan &p);

// Applies a layout PUT ({lights, rooms}) to `plan` after validating it
// against the plan's viewBox and the existing light/room ids.
bool apply_layout(const cJSON *body, const std::vector<std::string> &light_ids, const std::vector<std::string> &room_ids,
                  Plan &plan, std::string &error);

} // namespace lumen::floorplan
