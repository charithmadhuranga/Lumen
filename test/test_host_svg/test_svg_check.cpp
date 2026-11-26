// Host tests: firmware SVG safety checker (mandatory malicious-input suite,
// AGENTS.md §11). Every case is checked whole AND fed one byte at a time.
#include <cstring>
#include <string>
#include <vector>

#include <unity.h>

#include "core/cJSON.h"
#include "lumen/services/floorplan.h"
#include "lumen/services/svg_check.h"

using namespace lumen;

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

namespace
{

bool run(const std::string &s, std::string &err, svg::ViewBox *vb = nullptr)
{
    const bool whole = svg::check(s, err, vb);
    svg::Checker c;
    bool ok = true;
    for (char ch : s) {
        if (!c.feed(&ch, 1)) {
            ok = false;
            break;
        }
    }
    ok = ok && c.finish();
    TEST_ASSERT_EQUAL_MESSAGE(whole, ok, ("chunked result differs for: " + s.substr(0, 80)).c_str());
    return whole;
}

const char *kOk = R"S(<?xml version="1.0" encoding="UTF-8"?>
<!-- floor plan -->
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 800 600" width="800" height="600">
  <defs><linearGradient id="g"><stop offset="0" stop-color="#fff"/></linearGradient>
    <style>.wall{stroke:#333;fill:url(#g)} .t{font: 12px sans-serif}</style></defs>
  <g id="walls" class="wall" transform="translate(10,10)">
    <rect x="0" y="0" width="780" height="580" style="fill:none;stroke-width:4"/>
    <path d="M10 10 L 200 10 L 200 300 Z" fill="url('#g')"/>
    <use href="#walls"/><use xlink:href="#walls"/>
    <text class="t" x="20" y="40">Living room &amp; kitchen &gt; 20 m²</text>
    <image href="data:image/png;base64,iVBORw0KGgo=" width="10" height="10"/>
  </g>
</svg>
)S";

} // namespace

static void test_accepts_a_normal_floor_plan(void)
{
    std::string err;
    svg::ViewBox vb;
    TEST_ASSERT_TRUE_MESSAGE(run(kOk, err, &vb), err.c_str());
    TEST_ASSERT_TRUE(vb.w == 800);
    TEST_ASSERT_TRUE(vb.h == 600);

    TEST_ASSERT_TRUE(run(R"S(<svg width="120px" height="80"><circle r="5"/></svg>)S", err, &vb));
    TEST_ASSERT_TRUE(vb.w == 120);                   // width/height fallback
    TEST_ASSERT_TRUE(run(R"S(<svg viewBox="-10,-10,20,20"><g/></svg>)S", err, &vb));
    TEST_ASSERT_TRUE(vb.x == -10);
    TEST_ASSERT_TRUE(run(R"S(<svg:svg xmlns:svg="http://www.w3.org/2000/svg" viewBox="0 0 1 1"><svg:g/></svg:svg>)S", err));
}

static void test_rejects_malicious_samples(void)
{
    const char *bad[] = {
        R"S(<svg viewBox="0 0 1 1"><script>alert(1)</script></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><SCRIPT>alert(1)</SCRIPT></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><svg:script xmlns:svg="x">1</svg:script></svg>)S",
        R"S(<svg viewBox="0 0 1 1" onload="alert(1)"></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><rect ONCLICK="x()"/></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><rect onmouseover = 'x()'/></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><a href="javascript:alert(1)"><text>x</text></a></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><use href="javascript:alert(1)"/></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><use href=" JaVaScRiPt:alert(1)"/></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><use xlink:href="http://evil/x.svg#a"/></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><use href="//evil/x.svg#a"/></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><image href="http://evil/track.png"/></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><image href="data:image/svg+xml;base64,PHN2Zz4="/></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><image href="data:text/html,<script>1</script>"/></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><foreignObject><div xmlns="http://www.w3.org/1999/xhtml">x</div></foreignObject></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><iframe src="x"/></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><embed src="x"/></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><object data="x"/></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><style>@import url(http://evil/x.css);</style></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><style>rect{fill:url(http://evil/p)}</style></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><style><![CDATA[ @IMPORT 'x'; ]]></style></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><rect style="background:url(https://evil/b)"/></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><rect fill="url(http://evil/#g)"/></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><rect style="width:expression(alert(1))"/></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><set attributeName="href" to="javascript:alert(1)"/></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><animate attributeName="href" values="javascript:alert(1)"/></svg>)S",
        R"S(<!DOCTYPE svg [<!ENTITY a "aaaaaaaaaa">]><svg viewBox="0 0 1 1">&a;</svg>)S",
        R"S(<svg viewBox="0 0 1 1"><!ENTITY x SYSTEM "file:///etc/passwd"></svg>)S",
        R"S(<?xml-stylesheet href="http://evil/x.css"?><svg viewBox="0 0 1 1"/>)S",
        R"S(<html><svg viewBox="0 0 1 1"/></html>)S",
        R"S(<svg viewBox="0 0 1 1"></svg><script>alert(1)</script>)S",
        R"S(<svg viewBox="0 0 1 1"><rect x=1/></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><rect disabled/></svg>)S",
        R"S(<svg viewBox="0 0 1 1"><g>)S",
        R"S(<svg viewBox="0 0 1 1"><rect title="a>b" onload="x"/></svg>)S",
        R"S(<svg><g/></svg>)S",                     // no viewBox / size
        R"S()S",
        R"S(just text)S",
    };
    for (const char *b : bad) {
        std::string err;
        TEST_ASSERT_FALSE_MESSAGE(run(b, err), b);
        TEST_ASSERT_TRUE_MESSAGE(!err.empty(), b);
    }
}

static void test_quoted_angle_bracket_is_not_a_tag_end(void)
{
    std::string err;
    TEST_ASSERT_TRUE_MESSAGE(run(R"S(<svg viewBox="0 0 1 1"><rect class="a>b"/></svg>)S", err), err.c_str());
}

static void test_limits(void)
{
    std::string err;
    // Element-count bomb.
    std::string bomb = "<svg viewBox=\"0 0 1 1\">";
    for (int i = 0; i < 4100; ++i) bomb += "<g/>";
    bomb += "</svg>";
    TEST_ASSERT_FALSE(svg::check(bomb, err));
    TEST_ASSERT_NOT_NULL(std::strstr(err.c_str(), "4000"));
    // Oversized file.
    std::string big = "<svg viewBox=\"0 0 1 1\"><desc>" + std::string(130 * 1024, 'a') + "</desc></svg>";
    TEST_ASSERT_FALSE(svg::check(big, err));
    // Nesting depth.
    std::string deep = "<svg viewBox=\"0 0 1 1\">";
    for (int i = 0; i < 70; ++i) deep += "<g>";
    for (int i = 0; i < 70; ++i) deep += "</g>";
    deep += "</svg>";
    TEST_ASSERT_FALSE(svg::check(deep, err));
    // A very long path (real floor plans) is fine; a very long href is not.
    std::string path = "<svg viewBox=\"0 0 1 1\"><path d=\"M0 0";
    while (path.size() < 100 * 1024) path += " L1 1";
    path += "\"/></svg>";
    TEST_ASSERT_TRUE_MESSAGE(svg::check(path, err), err.c_str());
    std::string href = "<svg viewBox=\"0 0 1 1\"><use href=\"#" + std::string(20 * 1024, 'a') + "\"/></svg>";
    TEST_ASSERT_FALSE(svg::check(href, err));
}


static void test_floorplan_layout_validation(void)
{
    floorplan::Plan plan;
    plan.id = "ground";
    plan.vb[0] = 0; plan.vb[1] = 0; plan.vb[2] = 800; plan.vb[3] = 600;
    const std::vector<std::string> lights = {"ceiling", "tv_strip"}, rooms = {"living"};
    std::string err;
    cJSON *ok = cJSON_Parse(R"S({"lights":[{"lightId":"ceiling","x":100,"y":50.5,"radius":40,"color":"#ffaa00","icon":"bulb"},
        {"lightId":"tv_strip","x":-20,"y":610,"radius":10}],
        "rooms":[{"roomId":"living","points":[[0,0],[400,0],[400,300]]}]})S");
    TEST_ASSERT_TRUE_MESSAGE(floorplan::apply_layout(ok, lights, rooms, plan, err), err.c_str());
    cJSON_Delete(ok);
    TEST_ASSERT_EQUAL(2, plan.lights.size());
    // Round trip keeps viewBox coordinates exactly (shared by both renderers).
    cJSON *j = floorplan::to_json(plan);
    floorplan::Plan back;
    TEST_ASSERT_TRUE(floorplan::from_json(j, back, err));
    cJSON_Delete(j);
    TEST_ASSERT_TRUE(back.lights[0].x == 100 && back.lights[0].y == 50.5 && back.lights[1].x == -20);
    TEST_ASSERT_EQUAL_STRING("#ffaa00", back.lights[0].color.c_str());
    TEST_ASSERT_EQUAL(3, back.rooms[0].points.size());

    const char *bad[] = {
        R"S({"lights":[{"lightId":"ghost","x":1,"y":1,"radius":5}]})S",                       // unknown light
        R"S({"lights":[{"lightId":"ceiling","x":1,"y":1,"radius":5},{"lightId":"ceiling","x":2,"y":2,"radius":5}]})S",
        R"S({"lights":[{"lightId":"ceiling","x":5000,"y":1,"radius":5}]})S",                  // outside
        R"S({"lights":[{"lightId":"ceiling","x":1,"y":1,"radius":0}]})S",
        R"S({"lights":[{"lightId":"ceiling","x":1,"y":1,"radius":900}]})S",
        R"S({"lights":[{"lightId":"ceiling","x":1,"y":1,"radius":5,"color":"red"}]})S",
        R"S({"lights":[{"lightId":"ceiling","x":1,"y":1,"radius":5,"icon":"<b>"}]})S",
        R"S({"lights":[{"lightId":"ceiling","x":"1","y":1,"radius":5}]})S",
        R"S({"rooms":[{"roomId":"living","points":[[0,0],[1,1]]}]})S",                       // < 3 points
        R"S({"rooms":[{"roomId":"attic","points":[[0,0],[1,1],[2,2]]}]})S",
        R"S({"rooms":[{"roomId":"living","points":[[0,0],[1,1],["a",2]]}]})S",
        R"S({"lights":{}})S",
        R"S([])S",
    };
    for (const char *b : bad) {
        cJSON *o = cJSON_Parse(b);
        floorplan::Plan before = plan;
        TEST_ASSERT_FALSE_MESSAGE(floorplan::apply_layout(o, lights, rooms, plan, err), b);
        TEST_ASSERT_EQUAL_MESSAGE(before.lights.size(), plan.lights.size(), b); // unchanged on error
        cJSON_Delete(o);
    }
    TEST_ASSERT_TRUE(floorplan::valid_id("first-floor_2"));
    TEST_ASSERT_FALSE(floorplan::valid_id("../x"));
    TEST_ASSERT_FALSE(floorplan::valid_id("Upper"));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_accepts_a_normal_floor_plan);
    RUN_TEST(test_rejects_malicious_samples);
    RUN_TEST(test_quoted_angle_bracket_is_not_a_tag_end);
    RUN_TEST(test_limits);
    RUN_TEST(test_floorplan_layout_validation);
    return UNITY_END();
}
