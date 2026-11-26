#include "lumen/services/svg_check.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace lumen::svg
{

namespace
{

constexpr std::size_t kMaxName = 64;
constexpr std::size_t kMaxValue = 8 * 1024;   // checked attribute values
constexpr std::size_t kMaxStyle = 32 * 1024;  // <style> contents
constexpr std::size_t kMaxSpecial = 64 * 1024; // comment / CDATA / PI
constexpr int kMaxDepth = 64;

std::string lower(std::string s)
{
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string local_name(const std::string &n)
{
    const std::size_t colon = n.find(':');
    return colon == std::string::npos ? n : n.substr(colon + 1);
}

bool forbidden_element(const std::string &n)
{
    static const char *const bad[] = {"script", "foreignobject", "iframe", "object", "embed", "audio", "video",
                                      "a", "animate", "animatemotion", "animatetransform", "animatecolor",
                                      "set", "handler", "listener", "frame", "frameset", "link", "meta", "base"};
    for (const char *b : bad) {
        if (n == b) return true;
    }
    return false;
}

// Attributes whose (possibly huge) values are pure geometry/numbers.
bool skip_value(const std::string &n)
{
    return n == "d" || n == "points" || n == "transform" || n == "x" || n == "y" || n == "width" || n == "height";
}

std::string trim(const std::string &s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool safe_data_image(const std::string &v)
{
    for (const char *p : {"data:image/png", "data:image/jpeg", "data:image/jpg", "data:image/gif", "data:image/webp"}) {
        const std::size_t n = std::strlen(p);
        if (v.compare(0, n, p) == 0 && (v.size() == n || v[n] == ';' || v[n] == ',')) return true;
    }
    return false;
}

// Every url( must reference a local fragment: url(#id), url('#id'), url("#id").
bool urls_local(const std::string &low)
{
    std::size_t pos = 0;
    while ((pos = low.find("url(", pos)) != std::string::npos) {
        std::size_t i = pos + 4;
        while (i < low.size() && (std::isspace(static_cast<unsigned char>(low[i])) || low[i] == '\'' || low[i] == '"')) ++i;
        if (i >= low.size() || low[i] != '#') return false;
        pos = i;
    }
    return true;
}

bool parse_number(const std::string &s, double &out)
{
    char *end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end != s.c_str();
}

} // namespace

bool Checker::fail(const std::string &why)
{
    if (!failed_) error_ = why;
    failed_ = true;
    return false;
}

bool Checker::check_css(const std::string &css)
{
    const std::string low = lower(css);
    for (const char *bad : {"@import", "expression(", "javascript:", "vbscript:", "behavior:", "-moz-binding", "data:text"}) {
        if (low.find(bad) != std::string::npos) return fail(std::string("CSS contains '") + bad + "'");
    }
    if (!urls_local(low)) return fail("CSS references an external url()");
    return true;
}

bool Checker::check_attr(const std::string &raw_name, const std::string &value)
{
    const std::string name = lower(raw_name);
    const std::string local = local_name(name);
    if (local.size() > 2 && local.compare(0, 2, "on") == 0) return fail("event handler attribute '" + raw_name + "'");
    if (name == "xmlns" || name.compare(0, 6, "xmlns:") == 0) return true; // namespace URIs are not fetched
    if (skip_value(local)) return true;
    const std::string low = lower(trim(value));
    for (const char *bad : {"javascript:", "vbscript:", "data:text", "data:application"}) {
        if (low.find(bad) != std::string::npos) return fail("attribute '" + raw_name + "' contains '" + bad + "'");
    }
    if (local == "href" || local == "src") {
        if (!(low.empty() || low[0] == '#' || safe_data_image(low))) return fail("external reference in '" + raw_name + "'");
    }
    if (local == "style" && !check_css(value)) return false;
    if (!urls_local(low)) return fail("external url() in '" + raw_name + "'");
    return true;
}

bool Checker::feed(const char *data, std::size_t len)
{
    if (failed_) return false;
    total_ += len;
    if (total_ > kMaxBytes) return fail("file is larger than 128 KB");

    for (std::size_t i = 0; i < len; ++i) {
        const char c = data[i];
        switch (st_) {
        case St::kText:
            if (c == '<') {
                st_ = St::kTag;
                buf_ = "<";
                quote_ = 0;
            } else if (in_style_) {
                if (text_.size() >= kMaxStyle) return fail("<style> block too large");
                text_.push_back(c);
            } else if (root_closed_ && !std::isspace(static_cast<unsigned char>(c))) {
                return fail("content after the root <svg> element");
            }
            break;
        case St::kTag:
            buf_.push_back(c);
            if (buf_.size() == 2 && c == '?') {
                st_ = St::kPi;
                break;
            }
            if (buf_.size() == 4 && buf_ == "<!--") {
                st_ = St::kComment;
                break;
            }
            if (buf_.size() == 9 && buf_ == "<![CDATA[") {
                st_ = St::kCdata;
                buf_.clear();
                break;
            }
            if (buf_.size() >= 2 && buf_[1] == '!' && buf_.size() <= 9 && std::string("<!--").compare(0, buf_.size(), buf_) != 0 &&
                std::string("<![CDATA[").compare(0, buf_.size(), buf_) != 0) {
                return fail("DOCTYPE/ENTITY declarations are not allowed");
            }
            // Element tags are scanned by scan_tag() once complete; long
            // geometry values are truncated while reading (below).
            if (buf_[1] != '!' && buf_[1] != '?') {
                // Quote state is tracked per character (O(1)), so a '>'
                // inside an attribute value does not end the tag.
                if (quote_) {
                    if (c == quote_) quote_ = 0;
                } else if (c == '"' || c == '\'') {
                    quote_ = c;
                }
                const char quote = quote_;
                if (!quote && c == '>') {
                    st_ = St::kText;
                    if (!scan_tag(buf_)) return false;
                    buf_.clear();
                } else if (buf_.size() > kMaxValue + 256) {
                    // Very long tag: keep scanning but compact geometry values.
                    // Strategy: if we are inside a quoted value of a geometry
                    // attribute, drop characters; otherwise the tag is too big.
                    std::size_t eq = buf_.rfind('=');
                    std::size_t nstart = eq;
                    while (nstart > 0 && !std::isspace(static_cast<unsigned char>(buf_[nstart - 1]))) --nstart;
                    const std::string attr = eq == std::string::npos ? "" : lower(local_name(buf_.substr(nstart, eq - nstart)));
                    if (quote && skip_value(trim(attr))) {
                        // Truncate the value: keep the opening quote only.
                        std::size_t open = buf_.find_first_of("\"'", eq);
                        buf_.resize(open + 1);
                    } else {
                        return fail("element or attribute value too large");
                    }
                }
            }
            break;
        case St::kComment:
            buf_.push_back(c);
            if (buf_.size() > kMaxSpecial) return fail("comment too large");
            if (buf_.size() >= 7 && buf_.compare(buf_.size() - 3, 3, "-->") == 0) {
                st_ = St::kText;
                buf_.clear();
            }
            break;
        case St::kCdata:
            buf_.push_back(c);
            if (buf_.size() > kMaxSpecial) return fail("CDATA section too large");
            if (buf_.size() >= 3 && buf_.compare(buf_.size() - 3, 3, "]]>") == 0) {
                if (in_style_) text_ += buf_.substr(0, buf_.size() - 3);
                else if (lower(buf_).find("<script") != std::string::npos) return fail("script in CDATA");
                st_ = St::kText;
                buf_.clear();
            }
            break;
        case St::kPi:
            buf_.push_back(c);
            if (buf_.size() > 512) return fail("processing instruction too large");
            if (c == '>' && buf_.size() >= 4 && buf_[buf_.size() - 2] == '?') {
                if (seen_root_ || lower(buf_).compare(0, 6, "<?xml ") != 0) {
                    return fail("processing instructions are not allowed");
                }
                st_ = St::kText;
                buf_.clear();
            }
            break;
        }
    }
    return true;
}

bool Checker::scan_tag(const std::string &tag)
{
    // tag = "<name attr="v" ...>" or "</name>" or "<name .../>"
    std::size_t i = 1;
    const bool closing = i < tag.size() && tag[i] == '/';
    if (closing) ++i;
    std::size_t n0 = i;
    while (i < tag.size() && !std::isspace(static_cast<unsigned char>(tag[i])) && tag[i] != '>' && tag[i] != '/') ++i;
    const std::string raw = tag.substr(n0, i - n0);
    if (raw.empty() || raw.size() > kMaxName) return fail("malformed tag");
    const std::string name = local_name(lower(raw));
    if (forbidden_element(name)) return fail("<" + raw + "> is not allowed");

    if (closing) {
        if (name == "style") {
            if (!check_css(text_)) return false;
            text_.clear();
            in_style_ = false;
        }
        if (--depth_ < 0) return fail("unbalanced tags");
        if (depth_ == 0) root_closed_ = true;
        return true;
    }

    if (root_closed_) return fail("content after the root <svg> element");
    const bool is_root = !seen_root_;
    if (is_root && name != "svg") return fail("the document root must be <svg>");
    seen_root_ = true;
    if (++elements_ > kMaxElements) return fail("more than 4000 elements");

    // Attributes.
    bool self_close = false;
    double w = 0, h = 0;
    while (i < tag.size()) {
        while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i]))) ++i;
        if (i >= tag.size()) break;
        if (tag[i] == '>') break;
        if (tag[i] == '/') {
            self_close = true;
            ++i;
            continue;
        }
        std::size_t a0 = i;
        while (i < tag.size() && tag[i] != '=' && !std::isspace(static_cast<unsigned char>(tag[i])) && tag[i] != '>' && tag[i] != '/') ++i;
        const std::string aname = tag.substr(a0, i - a0);
        while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i]))) ++i;
        if (i >= tag.size() || tag[i] != '=') return fail("attribute '" + aname + "' has no value");
        ++i;
        while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i]))) ++i;
        if (i >= tag.size() || (tag[i] != '"' && tag[i] != '\'')) return fail("unquoted attribute '" + aname + "'");
        const char q = tag[i++];
        const std::size_t v0 = i;
        while (i < tag.size() && tag[i] != q) ++i;
        if (i >= tag.size()) return fail("unterminated attribute '" + aname + "'");
        const std::string value = tag.substr(v0, i - v0);
        ++i;
        if (aname.empty() || aname.size() > kMaxName) return fail("malformed attribute");
        if (!check_attr(aname, value)) return false;
        if (is_root) {
            const std::string an = lower(aname);
            if (an == "viewbox") {
                std::string v = value;
                for (char &ch : v) {
                    if (ch == ',') ch = ' ';
                }
                char *p = const_cast<char *>(v.c_str());
                char *end = nullptr;
                double vals[4];
                int got = 0;
                for (; got < 4; ++got) {
                    vals[got] = std::strtod(p, &end);
                    if (end == p) break;
                    p = end;
                }
                if (got == 4) vb_ = ViewBox{vals[0], vals[1], vals[2], vals[3]};
            } else if (an == "width") {
                parse_number(value, w);
            } else if (an == "height") {
                parse_number(value, h);
            }
        }
    }
    if (is_root && !vb_.valid() && w > 0 && h > 0) vb_ = ViewBox{0, 0, w, h};
    if (!self_close) {
        if (++depth_ > kMaxDepth) return fail("nesting deeper than 64 levels");
        if (name == "style") {
            in_style_ = true;
            text_.clear();
        }
    } else if (is_root) {
        root_closed_ = true;
    }
    return true;
}

bool Checker::finish()
{
    if (failed_) return false;
    if (st_ != St::kText) return fail("the file ends inside a tag");
    if (!seen_root_) return fail("no <svg> element found");
    if (!root_closed_ || depth_ != 0) return fail("the <svg> element is not closed");
    if (!vb_.valid()) return fail("the <svg> element needs a viewBox (or width and height)");
    return true;
}

bool check(const std::string &svg, std::string &error, ViewBox *vb)
{
    Checker c;
    const bool ok = c.feed(svg.data(), svg.size()) && c.finish();
    error = c.error();
    if (vb) *vb = c.view_box();
    return ok;
}

} // namespace lumen::svg
