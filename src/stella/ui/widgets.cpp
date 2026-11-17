#include "widgets.h"

#include "layout.h"
#include "materials.h"

namespace stella {
namespace {

// Small controls get a slightly larger touch target than they draw.
Rect hitArea(const Rect &r, const Theme &t) {
  const int minimum = t.dp(32);
  const int dx = r.w < minimum ? (minimum - r.w) / 2 : 0;
  const int dy = r.h < minimum ? (minimum - r.h) / 2 : 0;
  return r.inset(-dx, -dy);
}

}  // namespace

bool trackPress(const Rect &hit, const Event &e, bool &pressed, Invalidator &inv, const Rect &repaint) {
  switch (e.type) {
    case EventType::Press:
      if (hit.contains(e.x, e.y)) {
        pressed = true;
        inv.invalidate(repaint);
      }
      return false;
    case EventType::Drag:
      if (pressed && !hit.contains(e.x, e.y)) {
        pressed = false;
        inv.invalidate(repaint);
      }
      return false;
    case EventType::Release:
      if (pressed) {
        pressed = false;
        inv.invalidate(repaint);
      }
      return false;
    case EventType::Tap:
      return hit.contains(e.x, e.y);
    default:
      return false;
  }
}

// ---------------------------------------------------------------- Button
void Button::draw(Canvas &c, const Theme &t) const {
  if (!c.visible(bounds.inset(-glassOutset(t)))) return;
  const Palette &p = t.color;
  const int radius = t.m.radiusControl;
  Color text = p.text;
  if (style == ButtonStyle::Glass) {
    paintGlass(c, t, bounds, radius);
    if (pressed) c.fillRoundRect(bounds, radius, p.accent, 46);
  } else if (style == ButtonStyle::Accent) {
    c.fillRoundRect(bounds.offset(0, t.dp(2)), radius, p.shadow, p.shadowAlpha);
    c.fillRoundRect(bounds, radius, p.accent, pressed ? 210 : 255);
    text = p.accentText;
  } else {
    if (pressed) c.fillRoundRect(bounds, radius, p.surface, 150);
    text = p.accent;
  }
  if (!enabled) text = p.textMuted;
  const Font &f = *t.font.body;
  const int iconSize = icon ? t.dp(16) : 0;
  const int gap = icon && label && label[0] ? t.dp(6) : 0;
  const int width = iconSize + gap + Canvas::textWidth(f, label);
  int x = bounds.centerX() - width / 2;
  if (icon) {
    icon(c, layout::leading(Rect(x, bounds.y, iconSize, bounds.h), iconSize), text);
    x += iconSize + gap;
  }
  c.drawTextIn(f, label, Rect(x, bounds.y, bounds.right() - x, bounds.h), text, Align::Left);
}

bool Button::handle(const Event &e, Invalidator &inv) {
  if (!enabled) return false;
  return trackPress(bounds, e, pressed, inv, bounds.inset(-6));
}

// ---------------------------------------------------------------- Toggle
Rect Toggle::track(const Theme &t) const {
  const int w = t.dp(40), h = t.dp(22);
  return Rect(bounds.right() - w, bounds.y + (bounds.h - h) / 2, w, h);
}

void Toggle::draw(Canvas &c, const Theme &t) const {
  const Rect tr = track(t);
  if (!c.visible(tr.inset(-2))) return;
  const Palette &p = t.color;
  if (on) {
    c.fillRoundRect(tr, tr.h / 2, p.accent);
  } else {
    c.fillRoundRect(tr, tr.h / 2, p.surface, 220);
    c.strokeRoundRect(tr, tr.h / 2, 1, p.line, (uint8_t)(p.lineAlpha * 2));
  }
  const int r = tr.h / 2 - t.dp(3);
  const int cx = on ? tr.right() - tr.h / 2 - 1 : tr.x + tr.h / 2;
  c.fillCircle(cx, tr.centerY(), r + 1, p.shadow, p.shadowAlpha);
  c.fillCircle(cx, tr.centerY() - 1, r, on ? p.accentText : p.glass);
}

bool Toggle::handle(const Event &e, Invalidator &inv) {
  if (e.type == EventType::Tap && bounds.contains(e.x, e.y)) {
    on = !on;
    inv.invalidate(bounds);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------- Slider
void Slider::draw(Canvas &c, const Theme &t) const {
  if (!c.visible(bounds)) return;
  const Palette &p = t.color;
  const int knob = t.dp(9), h = t.dp(6);
  const Rect track(bounds.x + knob, bounds.centerY() - h / 2, bounds.w - 2 * knob, h);
  const int span = maximum > minimum ? maximum - minimum : 1;
  const int kx = track.x + (int)((long)(value - minimum) * track.w / span);
  c.fillRoundRect(track, h / 2, p.line, (uint8_t)(p.lineAlpha * 2));
  c.fillRoundRect(Rect(track.x, track.y, kx - track.x + h / 2, h), h / 2, p.accent);
  c.fillCircle(kx, bounds.centerY() + 1, knob + 1, p.shadow, p.shadowAlpha);
  c.fillCircle(kx, bounds.centerY(), knob, p.surface);
  c.strokeCircle(kx, bounds.centerY(), knob, t.dp(2) > 2 ? t.dp(2) : 2, p.accent);
}

bool Slider::handle(const Event &e, Invalidator &inv, const Theme &t) {
  const Rect hit = hitArea(bounds, t);
  if (e.type == EventType::Press && hit.contains(e.x, e.y)) dragging = true;
  if (!dragging) return false;
  if (e.type == EventType::Release) {
    dragging = false;
    return false;
  }
  if (e.type != EventType::Press && e.type != EventType::Drag) return false;
  const int knob = t.dp(9);
  const int left = bounds.x + knob, width = bounds.w - 2 * knob;
  int x = e.x < left ? left : e.x > left + width ? left + width : e.x;
  const int next = minimum + (int)((long)(x - left) * (maximum - minimum) / (width > 0 ? width : 1));
  if (next == value) return false;
  value = next;
  inv.invalidate(bounds);
  return true;
}

// ---------------------------------------------------------------- Segmented
Rect Segmented::segment(int i) const {
  const int pad = 3;
  const Rect inner = bounds.inset(pad);
  const int w = count ? inner.w / count : inner.w;
  return Rect(inner.x + i * w, inner.y, i == count - 1 ? inner.right() - (inner.x + i * w) : w, inner.h);
}

void Segmented::draw(Canvas &c, const Theme &t) const {
  if (!c.visible(bounds.inset(-glassOutset(t)))) return;
  const Palette &p = t.color;
  paintGlass(c, t, bounds, bounds.h / 2, Elevation::Flat);
  for (int i = 0; i < count; ++i) {
    const Rect s = segment(i);
    if (i == selected) {
      c.fillRoundRect(s.offset(0, 1), s.h / 2, p.shadow, p.shadowAlpha);
      c.fillRoundRect(s, s.h / 2, p.surface);
      c.strokeRoundRect(s, s.h / 2, 1, p.line, p.lineAlpha);
    }
    c.drawTextIn(*t.font.caption, labels[i], s, i == selected ? p.accent : p.textMuted, Align::Center);
  }
}

bool Segmented::handle(const Event &e, Invalidator &inv) {
  if (e.type != EventType::Tap || !bounds.contains(e.x, e.y)) return false;
  for (int i = 0; i < count; ++i) {
    if (segment(i).inset(0, -4).contains(e.x, e.y) && i != selected) {
      selected = (uint8_t)i;
      inv.invalidate(bounds);
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------- ListRow
void ListRow::draw(Canvas &c, const Theme &t) const {
  if (!c.visible(bounds)) return;
  const Palette &p = t.color;
  if (pressed) c.fillRect(bounds.inset(1, 0), p.accent, 30);
  const int pad = t.m.padding;
  Rect content = bounds.inset(pad, 0);
  if (icon) {
    const int r = t.m.icon / 2 + t.dp(1);
    paintBubble(c, t, content.x + r, content.centerY(), r, tint);
    icon(c, layout::center(Rect(content.x, bounds.y, 2 * r, bounds.h), t.dp(14), t.dp(14)), p.accent);
    content = Rect(content.x + 2 * r + t.dp(8), content.y, content.w - 2 * r - t.dp(8), content.h);
  }
  if (chevron) {
    const int s = t.dp(12);
    icons::chevronRight(c, layout::trailing(content, s), p.textMuted);
    content.w -= s + t.dp(4);
  }
  if (detail && detail[0]) {
    const int dw = Canvas::textWidth(*t.font.caption, detail);
    const int maxDetail = content.w / 2;
    const int w = dw < maxDetail ? dw : maxDetail;
    c.drawTextIn(*t.font.caption, detail, Rect(content.right() - w, content.y, w, content.h), p.textMuted,
                 Align::Right);
    content.w -= w + t.dp(8);
  }
  c.drawTextIn(*t.font.body, title, content, p.text);
  if (separator) paintHairline(c, t, bounds.x + pad, bounds.bottom() - 1, bounds.w - 2 * pad);
}

bool ListRow::handle(const Event &e, Invalidator &inv) { return trackPress(bounds, e, pressed, inv, bounds); }

// ---------------------------------------------------------------- ListView
int ListView::maxOffset() const {
  const int content = count * rowHeight;
  return content > bounds.h ? content - bounds.h : 0;
}

void ListView::scrollTo(int index) {
  const int top = index * rowHeight;
  if (top < offset) offset = top;
  else if (top + rowHeight > offset + bounds.h) offset = top + rowHeight - bounds.h;
  if (offset > maxOffset()) offset = maxOffset();
  if (offset < 0) offset = 0;
}

void ListView::draw(Canvas &c, const Theme &t, DrawRow drawRow, void *context) const {
  if (!c.visible(bounds) || rowHeight <= 0) return;
  const Rect previous = c.pushClip(bounds);
  const int first = offset / rowHeight;
  for (int i = first; i < count; ++i) {
    const Rect row(bounds.x, bounds.y + i * rowHeight - offset, bounds.w, rowHeight);
    if (row.y >= bounds.bottom()) break;
    if (c.visible(row)) drawRow(c, t, i, row, i == pressed, context);
  }
  // Scroll indicator, only when the list overflows.
  if (maxOffset() > 0) {
    const int trackH = bounds.h - t.dp(8);
    const int thumbH = trackH * bounds.h / (count * rowHeight) > t.dp(16) ? trackH * bounds.h / (count * rowHeight) : t.dp(16);
    const int y = bounds.y + t.dp(4) + (trackH - thumbH) * offset / maxOffset();
    c.fillRoundRect(Rect(bounds.right() - t.dp(4), y, t.dp(3), thumbH), t.dp(2), t.color.textMuted, 110);
  }
  c.restoreClip(previous);
}

int ListView::handle(const Event &e, Invalidator &inv) {
  switch (e.type) {
    case EventType::Press:
      if (!bounds.contains(e.x, e.y)) return -1;
      dragStartOffset_ = offset;
      dragging_ = false;
      pressed = (e.y - bounds.y + offset) / rowHeight;
      if (pressed >= count) pressed = -1;
      inv.invalidate(bounds);
      return -1;
    case EventType::Drag: {
      if (!bounds.contains(e.startX, e.startY)) return -1;
      dragging_ = true;
      if (pressed >= 0) {
        pressed = -1;
        inv.invalidate(bounds);
      }
      int next = dragStartOffset_ - (e.y - e.startY);
      if (next < 0) next = 0;
      if (next > maxOffset()) next = maxOffset();
      if (next != offset) {
        offset = next;
        inv.invalidate(bounds);
      }
      return -1;
    }
    case EventType::Release:
      if (pressed >= 0) {
        pressed = -1;
        inv.invalidate(bounds);
      }
      dragging_ = false;
      return -1;
    case EventType::Tap: {
      if (!bounds.contains(e.x, e.y)) return -1;
      const int index = (e.y - bounds.y + offset) / rowHeight;
      return index < count ? index : -1;
    }
    default:
      return -1;
  }
}

}  // namespace stella
