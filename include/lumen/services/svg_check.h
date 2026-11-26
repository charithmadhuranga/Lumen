// Lumen — server-side SVG safety check for floor plans (SPEC §10.2).
//
// The web UI sanitises SVG with an allowlist before upload; the firmware does
// not trust that and REJECTS (never repairs) anything unsafe before it is
// stored. Streaming: fed in chunks, bounded memory, no XML parser.
//
// Rejected: <script>, <foreignObject>, <iframe>, <object>, <embed>, <audio>,
// <video>, <a>, <animate*>/<set> (can rewrite href), DOCTYPE/ENTITY (XXE,
// entity bombs), processing instructions other than the XML declaration,
// on* event attributes, javascript:/vbscript:/data:text, external href /
// xlink:href (only "#local" or data:image/{png,jpeg,gif,webp}), url() other
// than url(#local), @import, expression(), more than kMaxElements elements,
// files above kMaxBytes, documents whose root is not <svg>.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lumen::svg
{

constexpr std::size_t kMaxBytes = 128 * 1024;
constexpr std::size_t kMaxElements = 4000;

struct ViewBox
{
    double x = 0, y = 0, w = 0, h = 0;
    bool valid() const { return w > 0 && h > 0; }
};

class Checker
{
  public:
    // Returns false as soon as the input is known to be unsafe/invalid.
    bool feed(const char *data, std::size_t len);
    // Call after the last chunk.
    bool finish();

    const std::string &error() const { return error_; }
    std::size_t elements() const { return elements_; }
    const ViewBox &view_box() const { return vb_; }

  private:
    bool fail(const std::string &why);
    bool scan_tag(const std::string &tag);
    bool check_attr(const std::string &name, const std::string &value);
    bool check_css(const std::string &css);

    // Tokenizer state (one tag/comment/text run at a time, bounded).
    enum class St : uint8_t { kText, kTag, kComment, kCdata, kPi };
    St st_ = St::kText;
    std::string buf_;  // current tag / comment / pi
    char quote_ = 0;   // open quote inside the current tag
    std::string text_; // tail of text content (for <style> CSS)
    bool in_style_ = false;
    bool seen_root_ = false;
    bool root_closed_ = false;
    std::size_t total_ = 0;
    std::size_t elements_ = 0;
    int depth_ = 0;
    ViewBox vb_;
    std::string error_;
    bool failed_ = false;
};

// Convenience for whole buffers (tests, small inputs).
bool check(const std::string &svg, std::string &error, ViewBox *vb = nullptr);

} // namespace lumen::svg
