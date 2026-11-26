#include "lumen/services/floorplan.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace lumen::floorplan
{

namespace
{

bool num(const cJSON *o, const char *k, double &out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble)) return false;
    out = v->valuedouble;
    return true;
}

std::string str(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) ? v->valuestring : "";
}

bool valid_color(const std::string &c)
{
    if (c.size() != 7 || c[0] != '#') return false;
    return std::all_of(c.begin() + 1, c.end(), [](char ch) { return std::isxdigit(static_cast<unsigned char>(ch)); });
}

bool valid_icon(const std::string &s)
{
    return s.size() <= 24 && std::all_of(s.begin(), s.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

bool contains(const std::vector<std::string> &v, const std::string &s)
{
    return std::find(v.begin(), v.end(), s) != v.end();
}

} // namespace

bool valid_id(const std::string &id)
{
    if (id.empty() || id.size() > 32) return false;
    return std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; });
}

bool from_json(const cJSON *o, Plan &out, std::string &error)
{
    if (!cJSON_IsObject(o)) {
        error = "floor plan must be an object";
        return false;
    }
    out = Plan{};
    out.id = str(o, "id");
    out.name = str(o, "name");
    if (!valid_id(out.id)) {
        error = "invalid floor plan id";
        return false;
    }
    const cJSON *vb = cJSON_GetObjectItemCaseSensitive(o, "viewBox");
    if (cJSON_IsArray(vb) && cJSON_GetArraySize(vb) == 4) {
        for (int i = 0; i < 4; ++i) {
            const cJSON *n = cJSON_GetArrayItem(vb, i);
            if (cJSON_IsNumber(n)) out.vb[i] = n->valuedouble;
        }
    }
    out.has_svg = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "hasSvg"));
    const cJSON *ls = cJSON_GetObjectItemCaseSensitive(o, "lights");
    for (const cJSON *l = cJSON_IsArray(ls) ? ls->child : nullptr; l; l = l->next) {
        LightPin p;
        p.light_id = str(l, "lightId");
        num(l, "x", p.x);
        num(l, "y", p.y);
        num(l, "radius", p.radius);
        p.color = str(l, "color");
        p.icon = str(l, "icon");
        if (!p.light_id.empty()) out.lights.push_back(p);
    }
    const cJSON *rs = cJSON_GetObjectItemCaseSensitive(o, "rooms");
    for (const cJSON *r = cJSON_IsArray(rs) ? rs->child : nullptr; r; r = r->next) {
        RoomRegion rr;
        rr.room_id = str(r, "roomId");
        const cJSON *pts = cJSON_GetObjectItemCaseSensitive(r, "points");
        for (const cJSON *pt = cJSON_IsArray(pts) ? pts->child : nullptr; pt; pt = pt->next) {
            if (cJSON_IsArray(pt) && cJSON_GetArraySize(pt) == 2) {
                rr.points.emplace_back(cJSON_GetArrayItem(pt, 0)->valuedouble, cJSON_GetArrayItem(pt, 1)->valuedouble);
            }
        }
        if (!rr.room_id.empty()) out.rooms.push_back(rr);
    }
    return true;
}

cJSON *to_json(const Plan &p)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "schema_version", kSchemaVersion);
    cJSON_AddStringToObject(o, "id", p.id.c_str());
    cJSON_AddStringToObject(o, "name", p.name.c_str());
    cJSON *vb = cJSON_AddArrayToObject(o, "viewBox");
    for (double v : p.vb) cJSON_AddItemToArray(vb, cJSON_CreateNumber(v));
    cJSON_AddBoolToObject(o, "hasSvg", p.has_svg);
    cJSON *ls = cJSON_AddArrayToObject(o, "lights");
    for (const auto &l : p.lights) {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "lightId", l.light_id.c_str());
        cJSON_AddNumberToObject(j, "x", l.x);
        cJSON_AddNumberToObject(j, "y", l.y);
        cJSON_AddNumberToObject(j, "radius", l.radius);
        if (!l.color.empty()) cJSON_AddStringToObject(j, "color", l.color.c_str());
        if (!l.icon.empty()) cJSON_AddStringToObject(j, "icon", l.icon.c_str());
        cJSON_AddItemToArray(ls, j);
    }
    cJSON *rs = cJSON_AddArrayToObject(o, "rooms");
    for (const auto &r : p.rooms) {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "roomId", r.room_id.c_str());
        cJSON *pts = cJSON_AddArrayToObject(j, "points");
        for (const auto &pt : r.points) {
            cJSON *a = cJSON_CreateArray();
            cJSON_AddItemToArray(a, cJSON_CreateNumber(pt.first));
            cJSON_AddItemToArray(a, cJSON_CreateNumber(pt.second));
            cJSON_AddItemToArray(pts, a);
        }
        cJSON_AddItemToArray(rs, j);
    }
    return o;
}

bool apply_layout(const cJSON *body, const std::vector<std::string> &light_ids, const std::vector<std::string> &room_ids,
                  Plan &plan, std::string &error)
{
    if (!cJSON_IsObject(body)) {
        error = "JSON object expected";
        return false;
    }
    // Positions may sit slightly outside the drawing (10 % margin).
    const double mx = plan.vb[2] * 0.1, my = plan.vb[3] * 0.1;
    const double x0 = plan.vb[0] - mx, x1 = plan.vb[0] + plan.vb[2] + mx;
    const double y0 = plan.vb[1] - my, y1 = plan.vb[1] + plan.vb[3] + my;
    const double rmax = std::max(plan.vb[2], plan.vb[3]) / 2;
    auto inside = [&](double x, double y) { return x >= x0 && x <= x1 && y >= y0 && y <= y1; };

    std::vector<LightPin> lights;
    const cJSON *ls = cJSON_GetObjectItemCaseSensitive(body, "lights");
    if (ls != nullptr && !cJSON_IsArray(ls)) {
        error = "lights must be an array";
        return false;
    }
    for (const cJSON *l = ls ? ls->child : nullptr; l; l = l->next) {
        if (lights.size() >= kMaxLights) {
            error = "at most 64 lights per floor plan";
            return false;
        }
        LightPin p;
        p.light_id = str(l, "lightId");
        if (!contains(light_ids, p.light_id)) {
            error = "unknown light '" + p.light_id.substr(0, 48) + "'";
            return false;
        }
        if (std::any_of(lights.begin(), lights.end(), [&](const LightPin &q) { return q.light_id == p.light_id; })) {
            error = "light '" + p.light_id + "' is placed twice";
            return false;
        }
        if (!num(l, "x", p.x) || !num(l, "y", p.y) || !inside(p.x, p.y)) {
            error = "position of '" + p.light_id + "' is outside the floor plan";
            return false;
        }
        if (!num(l, "radius", p.radius) || p.radius <= 0 || p.radius > rmax) {
            error = "glow radius of '" + p.light_id + "' out of range";
            return false;
        }
        p.color = str(l, "color");
        if (!p.color.empty() && !valid_color(p.color)) {
            error = "glow colour must be #rrggbb";
            return false;
        }
        p.icon = str(l, "icon");
        if (!valid_icon(p.icon)) {
            error = "invalid icon name";
            return false;
        }
        lights.push_back(p);
    }

    std::vector<RoomRegion> rooms;
    const cJSON *rs = cJSON_GetObjectItemCaseSensitive(body, "rooms");
    if (rs != nullptr && !cJSON_IsArray(rs)) {
        error = "rooms must be an array";
        return false;
    }
    for (const cJSON *r = rs ? rs->child : nullptr; r; r = r->next) {
        if (rooms.size() >= kMaxRooms) {
            error = "at most 32 room regions";
            return false;
        }
        RoomRegion rr;
        rr.room_id = str(r, "roomId");
        if (!contains(room_ids, rr.room_id)) {
            error = "unknown room '" + rr.room_id.substr(0, 48) + "'";
            return false;
        }
        const cJSON *pts = cJSON_GetObjectItemCaseSensitive(r, "points");
        const int n = cJSON_IsArray(pts) ? cJSON_GetArraySize(pts) : 0;
        if (n < 3 || n > static_cast<int>(kMaxPoints)) {
            error = "a room region needs 3 to 32 points";
            return false;
        }
        for (const cJSON *pt = pts->child; pt; pt = pt->next) {
            const cJSON *a = cJSON_IsArray(pt) && cJSON_GetArraySize(pt) == 2 ? cJSON_GetArrayItem(pt, 0) : nullptr;
            const cJSON *b = a ? cJSON_GetArrayItem(pt, 1) : nullptr;
            if (!cJSON_IsNumber(a) || !cJSON_IsNumber(b) || !std::isfinite(a->valuedouble) || !std::isfinite(b->valuedouble) ||
                !inside(a->valuedouble, b->valuedouble)) {
                error = "room region of '" + rr.room_id + "' has an invalid point";
                return false;
            }
            rr.points.emplace_back(a->valuedouble, b->valuedouble);
        }
        rooms.push_back(rr);
    }
    plan.lights = std::move(lights);
    plan.rooms = std::move(rooms);
    return true;
}

} // namespace lumen::floorplan
