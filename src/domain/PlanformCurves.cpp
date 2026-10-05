#include "domain/PlanformCurves.h"

#include <QFile>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <functional>
#include <numbers>
#include <stdexcept>
#include <string>

namespace designrc::domain {
namespace {
constexpr double tolerance = 6.35;
constexpr double flattenTolerance = 0.01;
[[noreturn]] void fail(const std::string& message) {
  throw std::invalid_argument("LE/TE curves: " + message);
}
double number(const QString& text) {
  bool ok = false;
  const double value = text.trimmed().toDouble(&ok);
  if (!ok || !std::isfinite(value)) fail("invalid numeric coordinate or dimension.");
  return value;
}
struct Numbers {
  QString text;
  qsizetype pos{};
  void spaces() {
    while (pos < text.size() && (text[pos].isSpace() || text[pos] == ','))
      ++pos;
  }
  bool end() {
    spaces();
    return pos == text.size();
  }
  bool flag() {
    spaces();
    if (pos >= text.size() || (text[pos] != '0' && text[pos] != '1')) fail("invalid SVG arc flag.");
    return text[pos++] == '1';
  }
  double take() {
    spaces();
    static const QRegularExpression re{R"([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)"};
    const auto match = re.match(text, pos, QRegularExpression::NormalMatch,
                                QRegularExpression::AnchorAtOffsetMatchOption);
    if (!match.hasMatch()) fail("invalid path data.");
    pos += match.capturedLength();
    return number(match.captured());
  }
};
struct Matrix {
  double a{1}, b{}, c{}, d{1}, e{}, f{};
  Point2 map(Point2 p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
  Matrix operator*(const Matrix& r) const {
    return {a * r.a + c * r.b, b * r.a + d * r.b,     a * r.c + c * r.d,
            b * r.c + d * r.d, a * r.e + c * r.f + e, b * r.e + d * r.f + f};
  }
};
Matrix transform(const QString& text) {
  Matrix result;
  qsizetype pos = 0;
  static const QRegularExpression re{R"(\s*,?\s*([A-Za-z]+)\s*\(([^)]*)\))"};
  while (!text.mid(pos).trimmed().isEmpty()) {
    auto m = re.match(text, pos, QRegularExpression::NormalMatch,
                      QRegularExpression::AnchorAtOffsetMatchOption);
    if (!m.hasMatch()) fail("unsupported SVG transform.");
    pos += m.capturedLength();
    Numbers n{m.captured(2)};
    std::vector<double> v;
    while (!n.end())
      v.push_back(n.take());
    Matrix t;
    const auto name = m.captured(1);
    if (name == "matrix" && v.size() == 6)
      t = {v[0], v[1], v[2], v[3], v[4], v[5]};
    else if (name == "translate" && (v.size() == 1 || v.size() == 2)) {
      t.e = v[0];
      t.f = v.size() == 2 ? v[1] : 0;
    } else if (name == "scale" && (v.size() == 1 || v.size() == 2)) {
      t.a = v[0];
      t.d = v.size() == 2 ? v[1] : v[0];
    } else if (name == "rotate" && (v.size() == 1 || v.size() == 3)) {
      const double a = v[0] * std::numbers::pi / 180;
      t = {std::cos(a), std::sin(a), -std::sin(a), std::cos(a), 0, 0};
      if (v.size() == 3) t = Matrix{1, 0, 0, 1, v[1], v[2]} * t * Matrix{1, 0, 0, 1, -v[1], -v[2]};
    } else if (name == "skewX" && v.size() == 1)
      t.c = std::tan(v[0] * std::numbers::pi / 180);
    else if (name == "skewY" && v.size() == 1)
      t.b = std::tan(v[0] * std::numbers::pi / 180);
    else
      fail("unsupported SVG transform.");
    result = result * t;
  }
  return result;
}
using Path = std::vector<Point2>;
void append(Path& path, Point2 p) {
  if (!std::isfinite(p.x) || !std::isfinite(p.y)) fail("non-finite geometry.");
  if (path.size() > 100000) fail("curve is too complex.");
  if (path.empty() || std::hypot(p.x - path.back().x, p.y - path.back().y) > 1e-10)
    path.push_back(p);
}
void sample(Path& path, const std::function<Point2(double)>& f, double a = 0, double b = 1,
            int depth = 0) {
  const auto p = f(a), q = f(b);
  double error = 0;
  for (double t : {0.25, 0.5, 0.75}) {
    const auto s = f(a + (b - a) * t);
    error =
        std::max(error, std::hypot(s.x - (p.x + (q.x - p.x) * t), s.y - (p.y + (q.y - p.y) * t)));
  }
  if (error > flattenTolerance || depth < 2) {
    if (depth >= 22) fail("curve cannot be resolved to manufacturing precision.");
    const double m = (a + b) * 0.5;
    sample(path, f, a, m, depth + 1);
    sample(path, f, m, b, depth + 1);
  } else {
    append(path, p);
    append(path, q);
  }
}
void arc(Path& path, const Matrix& m, Point2 start, double rx, double ry, double rotation,
         bool large, bool sweep, Point2 end) {
  rx = std::abs(rx);
  ry = std::abs(ry);
  if (rx == 0 || ry == 0) {
    append(path, m.map(end));
    return;
  }
  const double phi = rotation * std::numbers::pi / 180, c = std::cos(phi), s = std::sin(phi);
  const double dx = (start.x - end.x) / 2, dy = (start.y - end.y) / 2;
  const double xp = c * dx + s * dy, yp = -s * dx + c * dy;
  if (std::hypot(dx, dy) < 1e-12) fail("closed or degenerate arc.");
  const double scale = std::sqrt(xp * xp / (rx * rx) + yp * yp / (ry * ry));
  if (scale > 1) {
    rx *= scale;
    ry *= scale;
  }
  const double k =
      (large == sweep ? -1 : 1) *
      std::sqrt(std::max(0.0, (rx * rx * ry * ry - rx * rx * yp * yp - ry * ry * xp * xp) /
                                  (rx * rx * yp * yp + ry * ry * xp * xp)));
  const double cxp = k * rx * yp / ry, cyp = -k * ry * xp / rx;
  const Point2 center{c * cxp - s * cyp + (start.x + end.x) / 2,
                      s * cxp + c * cyp + (start.y + end.y) / 2};
  const double first = std::atan2((yp - cyp) / ry, (xp - cxp) / rx);
  double delta = std::atan2((-yp - cyp) / ry, (-xp - cxp) / rx) - first;
  if (sweep && delta < 0) delta += 2 * std::numbers::pi;
  if (!sweep && delta > 0) delta -= 2 * std::numbers::pi;
  sample(path, [=](double t) {
    const double a = first + t * delta;
    return m.map({center.x + c * rx * std::cos(a) - s * ry * std::sin(a),
                  center.y + s * rx * std::cos(a) + c * ry * std::sin(a)});
  });
}
std::vector<Path> svgPath(const QString& data, const Matrix& matrix) {
  Numbers n{data};
  std::vector<Path> paths;
  Path out;
  Point2 p{}, previousControl{};
  char command = 0, previous = 0;
  bool moved = false;
  while (!n.end()) {
    if (n.text[n.pos].isLetter())
      command = n.text[n.pos++].toLatin1();
    else if (!command)
      fail("SVG path must begin with M.");
    const bool relative = command >= 'a' && command <= 'z';
    const char op = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));
    const auto point = [&]() {
      Point2 q{n.take(), n.take()};
      if (relative) {
        q.x += p.x;
        q.y += p.y;
      }
      return q;
    };
    if (op == 'Z') fail("only open paths are accepted.");
    if (!moved && op != 'M') fail("SVG path must begin with M.");
    if (op == 'M') {
      if (moved) {
        paths.push_back(std::move(out));
        out.clear();
        if (paths.size() >= 2) fail("the file must contain exactly two open paths.");
      }
      p = point();
      moved = true;
      append(out, matrix.map(p));
      command = relative ? 'l' : 'L';
    } else if (op == 'L' || op == 'H' || op == 'V') {
      if (op == 'L')
        p = point();
      else if (op == 'H')
        p.x = n.take() + (relative ? p.x : 0);
      else
        p.y = n.take() + (relative ? p.y : 0);
      append(out, matrix.map(p));
    } else if (op == 'C' || op == 'S' || op == 'Q' || op == 'T') {
      const auto a = p;
      Point2 b, c, d;
      if (op == 'S' || op == 'T') {
        const bool reflect =
            op == 'S' ? (previous == 'C' || previous == 'S') : (previous == 'Q' || previous == 'T');
        b = reflect ? Point2{2 * p.x - previousControl.x, 2 * p.y - previousControl.y} : p;
      } else
        b = point();
      if (op == 'C' || op == 'S') {
        c = point();
        d = point();
        previousControl = c;
      } else {
        d = point();
        previousControl = b;
        c = {d.x + 2 * (b.x - d.x) / 3, d.y + 2 * (b.y - d.y) / 3};
        b = {a.x + 2 * (b.x - a.x) / 3, a.y + 2 * (b.y - a.y) / 3};
      }
      sample(out, [=](double t) {
        double u = 1 - t;
        return matrix.map(
            {u * u * u * a.x + 3 * u * u * t * b.x + 3 * u * t * t * c.x + t * t * t * d.x,
             u * u * u * a.y + 3 * u * u * t * b.y + 3 * u * t * t * c.y + t * t * t * d.y});
      });
      p = d;
    } else if (op == 'A') {
      double rx = n.take(), ry = n.take(), angle = n.take();
      const bool large = n.flag(), sweep = n.flag();
      auto end = point();
      arc(out, matrix, p, rx, ry, angle, large, sweep, end);
      p = end;
    } else
      fail("unsupported SVG path command.");
    previous = op;
  }
  paths.push_back(std::move(out));
  return paths;
}
double physical(const QString& text) {
  static const QRegularExpression re{
      R"(^\s*([+\-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+\-]?\d+)?)\s*(mm|in)\s*$)"};
  const auto m = re.match(text);
  if (!m.hasMatch())
    fail("SVG width and height must specify physical units (mm/in); pixel, percentage and unitless "
         "dimensions are not accepted.");
  const double v = number(m.captured(1)) * (m.captured(2) == "in" ? 25.4 : 1);
  if (v <= 0) fail("SVG dimensions must be positive.");
  return v;
}
void validateSvgStyle(QString style) {
  style.remove(QRegularExpression{R"(/\*[\s\S]*?\*/)"});
  if (style.contains("@import", Qt::CaseInsensitive))
    fail("external SVG stylesheets are not supported.");
  static const QRegularExpression property{R"((?:^|[;{])\s*([\w-]+)\s*:)"};
  auto matches = property.globalMatch(style);
  while (matches.hasNext()) {
    const auto key = matches.next().captured(1).toLower();
    if (QStringList{"d", "x", "y", "x1", "x2", "y1", "y2", "cx", "cy", "r", "rx", "ry", "width",
                    "height", "transform", "transform-origin", "translate", "rotate", "scale"}
            .contains(key))
      fail("CSS geometry is not supported; use SVG coordinates and transform attributes.");
  }
}
std::vector<Path> readSvg(const QByteArray& bytes) {
  QXmlStreamReader xml{bytes};
  std::vector<Matrix> stack;
  std::vector<Path> paths;
  bool root = false;
  while (!xml.atEnd()) {
    xml.readNext();
    if (xml.isProcessingInstruction() && xml.processingInstructionTarget() == QStringLiteral("xml-stylesheet"))
      fail("external SVG stylesheets are not supported.");
    if (xml.isEntityReference()) fail("SVG external entities are not supported.");
    if (xml.isEndElement()) {
      if (!stack.empty()) stack.pop_back();
      continue;
    }
    if (!xml.isStartElement()) continue;
    const auto name = xml.name().toString();
    const auto a = xml.attributes();
    if (root && !xml.namespaceUri().isEmpty() &&
        xml.namespaceUri() != QStringLiteral("http://www.w3.org/2000/svg")) {
      xml.skipCurrentElement(); // Editor metadata (for example Inkscape namedview).
      continue;
    }
    if (name == "style") {
      validateSvgStyle(xml.readElementText());
      continue;
    }
    if (name == "metadata" || name == "title" || name == "desc" || name == "defs") {
      xml.skipCurrentElement();
      continue;
    }
    Matrix m = stack.empty() ? Matrix{} : stack.back();
    if (!root) {
      if (name != "svg") fail("not an SVG document.");
      root = true;
      const double w = physical(a.value("width").toString()),
                   h = physical(a.value("height").toString());
      if (a.hasAttribute("viewBox")) {
        Numbers n{a.value("viewBox").toString()};
        // viewBox minima locate the canvas, not the CAD drawing origin.
        (void)n.take();
        (void)n.take();
        double vw = n.take(), vh = n.take();
        if (!n.end() || vw <= 0 || vh <= 0) fail("invalid SVG viewBox.");
        double sx = w / vw, sy = h / vh;
        const auto aspect = a.value("preserveAspectRatio").toString().simplified();
        if (aspect != "none") {
          const bool slice = aspect.endsWith("slice");
          const double scale = slice ? std::max(sx, sy) : std::min(sx, sy);
          auto align = aspect.isEmpty() ? QString{"xMidYMid"} : aspect.split(' ').front();
          if (!QStringList{"xMinYMin", "xMidYMin", "xMaxYMin", "xMinYMid", "xMidYMid", "xMaxYMid",
                           "xMinYMax", "xMidYMax", "xMaxYMax"}
                   .contains(align))
            fail("unsupported SVG aspect-ratio alignment.");
          sx = sy = scale;
        }
        // Retain (0,0) in the file's user coordinate system. Applying viewport
        // padding or -minX/-minY here would move a correctly drawn LE root.
        m = {sx, 0, 0, sy, 0, 0};
      } else
        m = {25.4 / 96, 0, 0, 25.4 / 96, 0, 0};
    } else if (name == "svg")
      fail("nested SVG viewports are not supported; flatten the drawing first.");
    validateSvgStyle(a.value("style").toString());
    m = m * transform(a.value("transform").toString());
    stack.push_back(m);
    if (name == "svg" || name == "g") continue;
    if (name == "path") {
      auto subpaths = svgPath(a.value("d").toString(), m);
      for (auto& subpath : subpaths)
        paths.push_back(std::move(subpath));
    } else if (name == "line") {
      const auto val = [&](const char* key) {
        return a.hasAttribute(key) ? number(a.value(key).toString()) : 0.0;
      };
      paths.push_back({m.map({val("x1"), val("y1")}), m.map({val("x2"), val("y2")})});
    } else if (name == "polyline") {
      Numbers n{a.value("points").toString()};
      Path p;
      while (!n.end())
        append(p, m.map({n.take(), n.take()}));
      paths.push_back(std::move(p));
    } else
      fail("only two open paths, lines or polylines are allowed in SVG (unsupported element: " +
           name.toStdString() + ").");
    if (paths.size() > 2) fail("the file must contain exactly two open paths.");
  }
  if (xml.hasError() || !root) fail("invalid SVG XML.");
  return paths;
}
struct Group {
  int code;
  QString value;
};
using Entity = std::vector<Group>;
double field(const Entity& e, int code, double fallback = 0) {
  for (const auto& g : e)
    if (g.code == code) return number(g.value);
  return fallback;
}
std::vector<double> fields(const Entity& e, int code) {
  std::vector<double> v;
  for (const auto& g : e)
    if (g.code == code) v.push_back(number(g.value));
  return v;
}
void planar(const Entity& e) {
  for (const auto& g : e)
    if ((g.code >= 30 && g.code <= 38) || g.code == 39)
      if (number(g.value) != 0)
        fail("DXF geometry must lie exactly in the XY plane (Z=0), with no thickness.");
}
void bulge(Path& out, Point2 a, Point2 b, double v, const Matrix& m) {
  if (std::abs(v) < 1e-12) {
    append(out, m.map(a));
    append(out, m.map(b));
    return;
  }
  double radius = std::hypot(b.x - a.x, b.y - a.y) * (1 + v * v) / (4 * std::abs(v));
  arc(out, m, a, radius, radius, 0, std::abs(v) > 1, v > 0, b);
}
Path dxfEntity(const QString& type, const Entity& e, double scale) {
  planar(e);
  Path out;
  Matrix m{scale, 0, 0, scale, 0, 0};
  if (type == "LINE")
    return {m.map({field(e, 10), field(e, 20)}), m.map({field(e, 11), field(e, 21)})};
  const bool objectCoordinates = type == "LWPOLYLINE" || type == "ARC" ||
                                 (type == "POLYLINE" && !(static_cast<int>(field(e, 70)) & 8));
  double normalSign = 1;
  if (objectCoordinates || type == "ELLIPSE") {
    if (field(e, 210) != 0 || field(e, 220) != 0 || field(e, 230, 1) == 0)
      fail("DXF curve object coordinates must be in the XY plane; tilted extrusion planes are not "
           "supported.");
    normalSign = field(e, 230, 1) < 0 ? -1 : 1;
    if (objectCoordinates) m.a *= normalSign;
  }
  if (type == "LWPOLYLINE" || type == "POLYLINE") {
    if (static_cast<int>(field(e, 70)) & (1 | 16 | 32 | 64))
      fail("closed polylines and meshes are not supported.");
    struct Vertex {
      Point2 p;
      double bulge{};
    };
    std::vector<Vertex> v;
    for (const auto& g : e) {
      if (g.code == 10)
        v.push_back({{number(g.value), 0}, 0});
      else if (g.code == 20) {
        if (v.empty()) fail("invalid polyline vertex.");
        v.back().p.y = number(g.value);
      } else if (g.code == 42) {
        if (v.empty()) fail("invalid polyline bulge.");
        v.back().bulge = number(g.value);
      }
    }
    for (std::size_t i = 1; i < v.size(); ++i)
      bulge(out, v[i - 1].p, v[i].p, v[i - 1].bulge, m);
    return out;
  }
  if (type == "ARC" || type == "ELLIPSE") {
    const Point2 c{field(e, 10), field(e, 20)};
    double rx, ry, rotation, start, end;
    if (type == "ARC") {
      rx = ry = field(e, 40);
      rotation = 0;
      start = field(e, 50) * std::numbers::pi / 180;
      end = field(e, 51) * std::numbers::pi / 180;
    } else {
      rx = std::hypot(field(e, 11), field(e, 21));
      ry = rx * field(e, 40);
      rotation = std::atan2(field(e, 21), field(e, 11));
      start = field(e, 41);
      end = field(e, 42);
    }
    if (rx <= 0 || ry <= 0) fail("invalid DXF arc radius.");
    if (type == "ELLIPSE") ry *= normalSign;
    while (end <= start)
      end += 2 * std::numbers::pi;
    if (end - start >= 2 * std::numbers::pi - 1e-10) fail("closed ellipses are not accepted.");
    sample(out, [=](double t) {
      double a = start + t * (end - start);
      return m.map(
          {c.x + rx * std::cos(a) * std::cos(rotation) - ry * std::sin(a) * std::sin(rotation),
           c.y + rx * std::cos(a) * std::sin(rotation) + ry * std::sin(a) * std::cos(rotation)});
    });
    return out;
  }
  if (type == "SPLINE") {
    if (static_cast<int>(field(e, 70)) & 3) fail("closed or periodic splines are not accepted.");
    const int degree = static_cast<int>(field(e, 71));
    auto xs = fields(e, 10), ys = fields(e, 20), knots = fields(e, 40), weights = fields(e, 41);
    if (degree < 1 || degree > 10 || xs.size() != ys.size() ||
        xs.size() <= static_cast<std::size_t>(degree) || knots.size() != xs.size() + degree + 1)
      fail("DXF SPLINE requires valid control points and knots; export a control-point spline.");
    if (weights.empty()) weights.assign(xs.size(), 1);
    if (weights.size() != xs.size() || !std::is_sorted(knots.begin(), knots.end()))
      fail("invalid spline knots or weights.");
    for (double w : weights)
      if (w <= 0) fail("spline weights must be positive.");
    const auto eval = [&](double u) {
      int k = static_cast<int>(xs.size()) - 1;
      if (u < knots[xs.size()])
        k = static_cast<int>(std::upper_bound(knots.begin(), knots.end(), u) - knots.begin()) - 1;
      k = std::clamp(k, degree, static_cast<int>(xs.size()) - 1);
      std::vector<std::array<double, 3>> d;
      for (int j = 0; j <= degree; ++j) {
        int i = k - degree + j;
        d.push_back({xs[i] * weights[i], ys[i] * weights[i], weights[i]});
      }
      for (int r = 1; r <= degree; ++r)
        for (int j = degree; j >= r; --j) {
          int i = k - degree + j;
          double den = knots[i + degree - r + 1] - knots[i];
          double a = den > 0 ? (u - knots[i]) / den : 0;
          for (int q = 0; q < 3; ++q)
            d[j][q] = (1 - a) * d[j - 1][q] + a * d[j][q];
        }
      return m.map({d[degree][0] / d[degree][2], d[degree][1] / d[degree][2]});
    };
    for (std::size_t i = degree; i < xs.size(); ++i)
      if (knots[i + 1] > knots[i]) sample(out, eval, knots[i], knots[i + 1]);
    return out;
  }
  fail("unsupported DXF entity " + type.toStdString() +
       "; use LINE, ARC, ELLIPSE, SPLINE or an open polyline.");
}
std::vector<Path> readDxf(const QByteArray& bytes) {
  if (bytes.startsWith("AutoCAD Binary DXF"))
    fail("binary DXF is not supported; save an ASCII DXF.");
  auto lines = QString::fromUtf8(bytes).split('\n');
  Entity groups;
  for (qsizetype i = 0; i + 1 < lines.size(); i += 2) {
    bool ok = false;
    int code = lines[i].trimmed().toInt(&ok);
    if (!ok) fail("invalid ASCII DXF group code.");
    groups.push_back({code, lines[i + 1].trimmed()});
  }
  double scale = 0;
  bool entities = false;
  std::vector<Path> out;
  for (std::size_t i = 0; i < groups.size(); ++i)
    if (groups[i].code == 9 && groups[i].value == "$INSUNITS" && i + 1 < groups.size()) {
      double units = number(groups[i + 1].value);
      if (units == 1)
        scale = 25.4;
      else if (units == 4)
        scale = 1;
      else
        fail("DXF must declare physical units mm or inches in $INSUNITS.");
    }
  if (scale == 0) fail("unitless DXF files are not accepted; declare mm/in in $INSUNITS.");
  for (std::size_t i = 0; i < groups.size();) {
    if (groups[i].code == 2 && groups[i].value == "ENTITIES") {
      entities = true;
      ++i;
      continue;
    }
    if (groups[i].code == 0 && groups[i].value == "ENDSEC") {
      entities = false;
      ++i;
      continue;
    }
    if (!entities || groups[i].code != 0) {
      ++i;
      continue;
    }
    const auto type = groups[i++].value;
    Entity e;
    while (i < groups.size() && groups[i].code != 0)
      e.push_back(groups[i++]);
    // Standalone drafting/reference markers are not LE/TE paths. Skip the
    // entire entity before applying curve planarity or path-count checks.
    if (type == "POINT") continue;
    if (type == "POLYLINE") {
      // The POLYLINE header's dummy origin is not a vertex.
      planar(e);
      e.erase(std::remove_if(
                  e.begin(), e.end(),
                  [](const Group& g) { return g.code == 10 || g.code == 20 || g.code == 30; }),
              e.end());
      while (i < groups.size() && groups[i].value == "VERTEX") {
        ++i;
        Entity vertex;
        while (i < groups.size() && groups[i].code != 0)
          vertex.push_back(groups[i++]);
        planar(vertex);
        for (const auto& g : vertex)
          if (g.code == 10 || g.code == 20 || g.code == 42) e.push_back(g);
      }
      if (i >= groups.size() || groups[i].value != "SEQEND") fail("invalid DXF polyline sequence.");
      ++i;
      while (i < groups.size() && groups[i].code != 0)
        ++i;
    }
    out.push_back(dxfEntity(type, e, scale));
    if (out.size() > 2) fail("the DXF ENTITIES section must contain exactly two open paths.");
  }
  return out;
}
double xAt(const Path& path, double t) {
  t = std::clamp(t, 0.0, 1.0);
  const auto it =
      std::lower_bound(path.begin(), path.end(), t, [](Point2 p, double y) { return p.y < y; });
  if (it == path.begin()) return it->x;
  if (it == path.end()) return path.back().x;
  const auto a = *(it - 1), b = *it;
  return a.x + (b.x - a.x) * (t - a.y) / (b.y - a.y);
}
} // namespace

double PlanformCurves::leadingX(double t) const { return xAt(leading, t); }
double PlanformCurves::trailingX(double t) const { return xAt(trailing, t); }
double PlanformCurves::chord(double t) const { return trailingX(t) - leadingX(t); }
double PlanformCurves::area(double span) const {
  const auto integral = [](const Path& p) {
    double a = 0;
    for (std::size_t i = 1; i < p.size(); ++i)
      a += (p[i].y - p[i - 1].y) * (p[i].x + p[i - 1].x) * 0.5;
    return a;
  };
  return span * (integral(trailing) - integral(leading));
}
void PlanformCurves::validate(double span) const {
  if (empty()) return;
  validateGeometry();
  if (!std::isfinite(span) || span <= 0 || std::abs(leadingSourceSpan - span) > tolerance + 1e-9 ||
      std::abs(trailingSourceSpan - span) > tolerance + 1e-9)
    fail("each curve's span must be within 1/4 in (6.35 mm) of the Spec tab's Panel Span.");
}
void PlanformCurves::validateGeometry() const {
  if (empty()) return;
  if (!std::isfinite(leadingSourceSpan) || !std::isfinite(trailingSourceSpan) ||
      leadingSourceSpan <= 0 || trailingSourceSpan <= 0)
    fail("invalid saved physical span.");
  for (const auto* p : {&leading, &trailing}) {
    if (p->size() < 2 || p->size() > 100001 || p->front().y != 0 || p->back().y != 1)
      fail("invalid normalized curve endpoints.");
    for (std::size_t i = 0; i < p->size(); ++i) {
      if (!std::isfinite((*p)[i].x) || !std::isfinite((*p)[i].y) ||
          (i && (*p)[i].y <= (*p)[i - 1].y))
        fail("each curve must advance in positive Y with one X value per span station.");
    }
  }
  for (const auto* p : {&leading, &trailing})
    for (auto v : *p)
      if (chord(v.y) <= 1e-6) fail("LE and TE cross or have zero/negative chord.");
}
PlanformCurves importPlanformCurves(const std::filesystem::path& path, double span) {
  QFile f{QString::fromStdWString(path.wstring())};
  if (!f.open(QIODevice::ReadOnly)) fail("cannot open file.");
  if (f.size() > 16 * 1024 * 1024) fail("file exceeds 16 MB.");
  const auto bytes = f.readAll();
  const auto ext = QString::fromStdWString(path.extension().wstring()).toLower();
  auto paths = ext == ".svg"   ? readSvg(bytes)
               : ext == ".dxf" ? readDxf(bytes)
                               : std::vector<Path>{};
  if (paths.size() == 1)
    fail("only one curve/line was found in the file. Import requires exactly two open paths: "
         "one for the leading edge (LE) and one for the trailing edge (TE).");
  if (paths.empty())
    fail("no curves/lines were found in the file. Import requires exactly two open paths: "
         "one for the leading edge (LE) and one for the trailing edge (TE).");
  if (paths.size() != 2) fail("the file must contain exactly two open paths.");
  for (const auto& p : paths)
    if (p.size() < 2) fail("empty or degenerate path.");

  // Use the combined drawing bounds to identify the long axis before checking
  // roots or span. Swap coordinates, preserving their signs and physical units.
  Point2 minimum = paths.front().front(), maximum = minimum;
  for (const auto& p : paths)
    for (const auto& v : p) {
      minimum.x = std::min(minimum.x, v.x);
      minimum.y = std::min(minimum.y, v.y);
      maximum.x = std::max(maximum.x, v.x);
      maximum.y = std::max(maximum.y, v.y);
    }
  if (maximum.x - minimum.x > maximum.y - minimum.y)
    for (auto& p : paths)
      for (auto& v : p)
        std::swap(v.x, v.y);

  for (auto& p : paths) {
    if (p.front().y > p.back().y) std::reverse(p.begin(), p.end());
    if (p.back().y <= 0) fail("span must extend in the positive Y direction.");
    if (std::abs(p.front().y) > tolerance)
      fail(QString{"both root endpoints must be within 1/4 in (6.35 mm) of the X axis "
                   "(Y = 0). An endpoint at X = %1 mm, Y = %2 mm is outside this tolerance. "
                   "DesignRC uses X for chord and positive Y for span after automatically "
                   "swapping X/Y when the drawing is longer along X."}
               .arg(p.front().x, 0, 'f', 3)
               .arg(p.front().y, 0, 'f', 3)
               .toStdString());
    for (std::size_t i = 1; i < p.size(); ++i)
      if (p[i].y <= p[i - 1].y)
        fail("curves must extend in positive Y without loops, horizontal segments or backward span "
             "segments.");
  }
  const auto nearOrigin = [](Point2 p) { return std::hypot(p.x, p.y) <= tolerance; };
  const bool first = nearOrigin(paths[0].front()), second = nearOrigin(paths[1].front());
  if (first == second)
    fail("exactly one curve's root must lie within 1/4 in (6.35 mm) of the origin to identify the "
         "LE.");
  if (second) std::swap(paths[0], paths[1]);
  PlanformCurves result;
  const double originX = paths[0].front().x;
  result.leadingSourceSpan = paths[0].back().y - paths[0].front().y;
  result.trailingSourceSpan = paths[1].back().y - paths[1].front().y;
  for (auto& p : paths) {
    const double originY = p.front().y, length = p.back().y - originY;
    for (auto& v : p) {
      v.x -= originX;
      v.y = (v.y - originY) / length;
    }
    p.front().y = 0;
    p.back().y = 1;
  }
  result.leading = std::move(paths[0]);
  result.trailing = std::move(paths[1]);
  result.validate(span);
  return result;
}
} // namespace designrc::domain
