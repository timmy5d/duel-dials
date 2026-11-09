#include "icons.h"

#include <math.h>

namespace {

// Maps the 16x16 design grid onto the target and offers filled primitives.
// Only non-antialiased fills are used so icons work inside colour-keyed sprites.
struct Pen {
  lgfx::LovyanGFX &g;
  float cx, cy, k;
  uint32_t fg, bg;

  int X(float u) const { return (int)lroundf(cx + (u - 8) * k); }
  int Y(float v) const { return (int)lroundf(cy + (v - 8) * k); }
  int L(float d) const { return max(1, (int)lroundf(d * k)); }
  uint32_t c(bool cut) const { return cut ? bg : fg; }

  void circ(float x, float y, float r, bool cut = false) { g.fillCircle(X(x), Y(y), L(r), c(cut)); }
  void tri(float x0, float y0, float x1, float y1, float x2, float y2, bool cut = false) {
    g.fillTriangle(X(x0), Y(y0), X(x1), Y(y1), X(x2), Y(y2), c(cut));
  }
  void rect(float x, float y, float w, float h, bool cut = false) {
    g.fillRect(X(x), Y(y), L(w), L(h), c(cut));
  }
  void rrect(float x, float y, float w, float h, float r, bool cut = false) {
    g.fillRoundRect(X(x), Y(y), L(w), L(h), L(r), c(cut));
  }
  void arc(float x, float y, float r0, float r1, float a0, float a1, bool cut = false) {
    g.fillArc(X(x), Y(y), (int)lroundf(r0 * k), L(r1), a0, a1, c(cut));
  }
  // Thick line built from two triangles.
  void line(float x0, float y0, float x1, float y1, float w, bool cut = false) {
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len <= 0) return;
    float nx = -dy / len * w / 2, ny = dx / len * w / 2;
    tri(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, cut);
    tri(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, cut);
  }
  void star(float x, float y, float ro, float ri, int points, bool cut = false) {
    int n = points * 2;
    for (int i = 0; i < n; i++) {
      float a0 = (-90 + i * 180.f / points) * DEG_TO_RAD, a1 = (-90 + (i + 1) * 180.f / points) * DEG_TO_RAD;
      float r0 = i % 2 ? ri : ro, r1 = i % 2 ? ro : ri;
      tri(x, y, x + cosf(a0) * r0, y + sinf(a0) * r0, x + cosf(a1) * r1, y + sinf(a1) * r1, cut);
    }
  }
  // Arrow head on a circle of radius r at angle a (degrees), pointing clockwise (dir=1) or not.
  void arrowHead(float x, float y, float r, float a, int dir, float sz) {
    float t = a * DEG_TO_RAD;
    float px = x + cosf(t) * r, py = y + sinf(t) * r;
    float tx = -sinf(t) * dir, ty = cosf(t) * dir;  // tangent
    float nx = cosf(t), ny = sinf(t);               // radial
    tri(px + tx * sz, py + ty * sz, px + nx * sz * 0.9f, py + ny * sz * 0.9f, px - nx * sz * 0.9f,
        py - ny * sz * 0.9f);
  }
  void shield(float ox, float oy, float s, bool cut = false) {
    rect(ox + 3 * s, oy + 2 * s, 10 * s, 6.5f * s, cut);
    tri(ox + 3 * s, oy + 8.4f * s, ox + 13 * s, oy + 8.4f * s, ox + 8 * s, oy + 15 * s, cut);
  }
};

}  // namespace

void drawIcon(lgfx::LovyanGFX &g, Icon ic, int cx, int cy, int size, uint32_t fg, uint32_t bg) {
  Pen p{g, (float)cx, (float)cy, size / 16.f, fg, bg};
  switch (ic) {
    case IC_NONE:
    case IC_COUNT:
      break;

    case IC_COMMANDER:  // heater shield with a sword
      p.shield(0, 0, 1);
      p.line(8, 4.4f, 8, 9.4f, 1.8f, true);   // blade
      p.tri(7.1f, 4.5f, 8.9f, 4.5f, 8, 2.9f, true);  // point
      p.rect(5.2f, 9.3f, 5.6f, 1.4f, true);   // crossguard
      p.line(8, 10.6f, 8, 12.6f, 1.2f, true); // grip
      break;

    case IC_POISON:  // skull and crossbones
      p.line(2.4f, 9.6f, 13.6f, 15, 1.7f);
      p.line(13.6f, 9.6f, 2.4f, 15, 1.7f);
      for (float bx : {2.4f, 13.6f})
        for (float by : {9.6f, 15.f}) {
          p.circ(bx - 0.6f, by - 0.4f, 1.1f);
          p.circ(bx + 0.4f, by + 0.6f, 1.1f);
        }
      p.circ(8, 5.6f, 4.9f);
      p.rrect(5.3f, 7.6f, 5.4f, 4, 1.2f);
      p.circ(6.1f, 6.1f, 1.45f, true);
      p.circ(9.9f, 6.1f, 1.45f, true);
      p.tri(8, 7.7f, 7.1f, 9.3f, 8.9f, 9.3f, true);
      p.rect(6.6f, 10.4f, 0.8f, 1.3f, true);
      p.rect(8.6f, 10.4f, 0.8f, 1.3f, true);
      break;

    case IC_ENERGY:  // lightning bolt
      p.tri(10.6f, 0.5f, 2.8f, 9.6f, 8.8f, 9.6f);
      p.tri(5.4f, 15.5f, 13.2f, 6.4f, 7.2f, 6.4f);
      break;

    case IC_EXPERIENCE:  // star
      p.star(8, 8.6f, 7.8f, 3.3f, 5);
      break;

    case IC_RADIATION:  // radiation trefoil
      p.circ(8, 8, 7.8f);
      p.circ(8, 8, 6.9f, true);
      p.circ(8, 8, 1.5f);
      p.arc(8, 8, 2.5f, 6.3f, 60, 120);
      p.arc(8, 8, 2.5f, 6.3f, 180, 240);
      p.arc(8, 8, 2.5f, 6.3f, 300, 360);
      break;

    case IC_SPEED:  // speedometer
      p.arc(8, 10.5f, 5.4f, 7.4f, 180, 360);
      p.line(8, 10.5f, 12, 5.2f, 1.7f);
      p.circ(8, 10.5f, 2);
      break;

    case IC_TICKETS:  // ticket with notches and perforation
      p.rrect(1.2f, 4.2f, 13.6f, 7.6f, 1.5f);
      p.circ(1.2f, 8, 1.9f, true);
      p.circ(14.8f, 8, 1.9f, true);
      for (int i = 0; i < 3; i++) p.rect(10, 5.3f + i * 2, 0.9f, 1.2f, true);
      break;

    case IC_LIFE:  // heart
      p.circ(5.1f, 6, 3.4f);
      p.circ(10.9f, 6, 3.4f);
      p.tri(1.8f, 7.3f, 14.2f, 7.3f, 8, 14.6f);
      break;

    case IC_SKULL:
      p.circ(8, 7, 5.8f);
      p.rrect(4.6f, 9.4f, 6.8f, 5.2f, 1.4f);
      p.circ(5.7f, 7.6f, 1.7f, true);
      p.circ(10.3f, 7.6f, 1.7f, true);
      p.tri(8, 9.3f, 6.9f, 11.2f, 9.1f, 11.2f, true);
      p.rect(6.5f, 12.8f, 0.9f, 1.9f, true);
      p.rect(8.7f, 12.8f, 0.9f, 1.9f, true);
      break;

    case IC_MONARCH:  // crown
      p.rect(2.5f, 10.6f, 11, 3);
      p.tri(2.5f, 11, 2.5f, 4.6f, 6.6f, 11);
      p.tri(5, 11, 8, 3.2f, 11, 11);
      p.tri(9.4f, 11, 13.5f, 4.6f, 13.5f, 11);
      p.circ(2.5f, 4, 1.3f);
      p.circ(8, 2.6f, 1.3f);
      p.circ(13.5f, 4, 1.3f);
      break;

    case IC_INITIATIVE:  // dungeon gate with portcullis
      p.circ(8, 7, 5.6f);
      p.rect(2.4f, 7, 11.2f, 8.4f);
      p.circ(8, 8, 3.1f, true);
      p.rect(4.9f, 8, 6.2f, 7.4f, true);
      p.rect(6.2f, 5.5f, 1.1f, 9.9f);
      p.rect(8.8f, 5.5f, 1.1f, 9.9f);
      p.rect(4.9f, 10.4f, 6.2f, 1);
      break;

    case IC_ARCHENEMY:  // horned head
      p.circ(8, 10, 5.2f);
      p.tri(3.4f, 8.6f, 1.2f, 0.8f, 7, 6.4f);
      p.tri(12.6f, 8.6f, 14.8f, 0.8f, 9, 6.4f);
      p.tri(4.8f, 8.8f, 7.6f, 10.2f, 5.3f, 11.2f, true);
      p.tri(11.2f, 8.8f, 8.4f, 10.2f, 10.7f, 11.2f, true);
      p.rect(6.3f, 12.7f, 3.4f, 1, true);
      break;

    case IC_EMPEROR:  // laurel wreath with a jewel
      p.arc(8, 9, 5.2f, 6.1f, 115, 235);
      p.arc(8, 9, 5.2f, 6.1f, 305, 360);
      p.arc(8, 9, 5.2f, 6.1f, 0, 65);
      for (float a : {140.f, 180.f, 220.f, 320.f, 0.f, 40.f}) {
        float t = a * DEG_TO_RAD;
        p.circ(8 + cosf(t) * 7.2f, 9 + sinf(t) * 7.2f, 1.25f);
        p.circ(8 + cosf(t) * 4.3f, 9 + sinf(t) * 4.3f, 0.9f);
      }
      p.star(8, 2.6f, 2.6f, 1.1f, 5);
      break;

    case IC_SCHEMES:  // scroll
      p.rect(3.8f, 3.2f, 8.4f, 9.6f);
      p.rrect(2.2f, 1.2f, 11.6f, 3.2f, 1.6f);
      p.rrect(2.2f, 11.6f, 11.6f, 3.2f, 1.6f);
      p.rect(5.4f, 6, 5.2f, 0.9f, true);
      p.rect(5.4f, 8.1f, 5.2f, 0.9f, true);
      p.rect(5.4f, 10.2f, 3.4f, 0.9f, true);
      break;

    case IC_TURN:  // hourglass
      p.rect(3, 1, 10, 1.7f);
      p.rect(3, 13.3f, 10, 1.7f);
      p.tri(4, 2.6f, 12, 2.6f, 8, 8);
      p.tri(8, 8, 4, 13.4f, 12, 13.4f);
      break;

    case IC_SUN:
      p.circ(8, 8, 3.4f);
      for (int i = 0; i < 8; i++) {
        float t = i * 45 * DEG_TO_RAD;
        p.line(8 + cosf(t) * 5, 8 + sinf(t) * 5, 8 + cosf(t) * 7.4f, 8 + sinf(t) * 7.4f, 1.5f);
      }
      break;

    case IC_MOON:
      p.circ(8, 8, 6.4f);
      p.circ(11.4f, 5.4f, 5.1f, true);
      break;

    case IC_STORM:  // cloud with a bolt
      p.circ(4.8f, 7.4f, 2.8f);
      p.circ(8.8f, 5.4f, 3.7f);
      p.circ(12, 7.8f, 2.6f);
      p.rect(4.8f, 7.4f, 7.2f, 3.1f);
      p.tri(9.4f, 9.6f, 5.6f, 13.4f, 8.6f, 13.4f);
      p.tri(6.4f, 16, 10.6f, 12, 7.8f, 12);
      break;

    case IC_D20: {  // hexagon with a facet triangle
      for (int i = 0; i < 6; i++) {
        float a0 = (-90 + i * 60) * DEG_TO_RAD, a1 = (-90 + (i + 1) * 60) * DEG_TO_RAD;
        p.tri(8, 8, 8 + cosf(a0) * 7.4f, 8 + sinf(a0) * 7.4f, 8 + cosf(a1) * 7.4f, 8 + sinf(a1) * 7.4f);
      }
      p.line(8, 3.6f, 3.9f, 10.6f, 1, true);
      p.line(3.9f, 10.6f, 12.1f, 10.6f, 1, true);
      p.line(12.1f, 10.6f, 8, 3.6f, 1, true);
      break;
    }

    case IC_D6:
      p.rrect(1.8f, 1.8f, 12.4f, 12.4f, 2.6f);
      p.circ(5, 5, 1.4f, true);
      p.circ(8, 8, 1.4f, true);
      p.circ(11, 11, 1.4f, true);
      break;

    case IC_COIN:
      p.circ(8, 8, 7.2f);
      p.arc(8, 8, 4.6f, 5.6f, 0, 360, true);
      p.circ(8, 8, 1.7f, true);
      break;

    case IC_FLAG:
      p.line(3.6f, 1.2f, 3.6f, 15.2f, 1.6f);
      p.tri(4.4f, 1.6f, 13.8f, 5.2f, 4.4f, 8.8f);
      break;

    case IC_PLAY:
      p.tri(4.4f, 2.4f, 4.4f, 13.6f, 13.6f, 8);
      break;

    case IC_REFRESH:  // circular arrow, clockwise
      p.arc(8, 8, 4.6f, 6.6f, 0, 285);
      p.arrowHead(8, 8, 5.6f, 285, 1, 3.2f);
      break;

    case IC_UNDO:  // circular arrow, counter-clockwise
      p.arc(8, 8, 4.6f, 6.6f, 270, 360);
      p.arc(8, 8, 4.6f, 6.6f, 0, 165);
      p.arrowHead(8, 8, 5.6f, 255, -1, 3.2f);
      break;

    case IC_CARDS:  // two stacked cards
      p.rrect(1.2f, 4, 8.6f, 11, 1.5f);
      p.rrect(4.2f, 0.8f, 11.4f, 14.2f, 2, true);
      p.rrect(5.3f, 1.8f, 9.2f, 12.2f, 1.5f);
      break;

    case IC_PLAYERS:  // two people
      p.circ(11, 4.8f, 2.5f);
      p.rrect(7.4f, 8.4f, 7.6f, 6.4f, 3);
      p.circ(6, 6, 3.5f, true);
      p.rrect(0.8f, 9.4f, 10.4f, 6.8f, 3.4f, true);
      p.circ(6, 6, 2.7f);
      p.rrect(1.8f, 10.2f, 8.4f, 5.4f, 2.7f);
      break;

    case IC_PARTNERS:  // two small shields
      p.shield(-1.6f, -0.6f, 0.82f);
      p.shield(1.4f, 1.0f, 0.95f, true);
      p.shield(2.2f, 1.9f, 0.82f);
      break;

    case IC_THEME:  // half-filled circle
      p.arc(8, 8, 5.6f, 7.2f, 0, 360);
      p.arc(8, 8, 0, 5.6f, 270, 360);
      p.arc(8, 8, 0, 5.6f, 0, 90);
      break;

    case IC_EYE:
      g.fillEllipse(p.X(8), p.Y(8), p.L(7.4f), p.L(4.4f), fg);
      p.circ(8, 8, 2.9f, true);
      p.circ(8, 8, 1.6f);
      break;

    case IC_CMD_LIFE:  // shield with a heart cut out
      p.shield(0, 0, 1);
      p.circ(6.5f, 6.2f, 1.8f, true);
      p.circ(9.5f, 6.2f, 1.8f, true);
      p.tri(4.8f, 6.9f, 11.2f, 6.9f, 8, 10.8f, true);
      break;

    case IC_DIAL:  // knob
      p.arc(8, 8, 6, 7.5f, 0, 360);
      p.circ(8, 8, 4.4f);
      p.line(8, 8, 8, 4.3f, 1.4f, true);
      break;

    case IC_LEDS:  // arc of dots
      for (int i = 0; i < 5; i++) {
        float t = (-90 + i * 72) * DEG_TO_RAD;
        p.circ(8 + cosf(t) * 5.8f, 8 + sinf(t) * 5.8f, 1.8f);
      }
      p.circ(8, 8, 1.4f);
      break;

    case IC_BACK:  // chevron left
      p.line(10.6f, 2.4f, 5, 8, 2.3f);
      p.line(5, 8, 10.6f, 13.6f, 2.3f);
      p.circ(5, 8, 1.15f);
      break;

    case IC_SWORD:  // diagonal sword: combat damage
      p.line(5.6f, 10.4f, 13.2f, 2.8f, 2.3f);
      p.tri(12.4f, 2.0f, 14.6f, 1.4f, 14.0f, 3.6f);
      p.line(3.0f, 8.0f, 8.0f, 13.0f, 1.8f);
      p.line(5.2f, 10.8f, 2.6f, 13.4f, 1.5f);
      p.circ(2.2f, 13.8f, 1.4f);
      break;

    case IC_LINK:  // two chain links
      p.arc(5.4f, 10.6f, 2.4f, 4.3f, 0, 360);
      p.arc(10.6f, 5.4f, 2.4f, 4.3f, 0, 360);
      p.line(5.6f, 10.4f, 10.4f, 5.6f, 2.0f);
      break;

    case IC_SWATCH:  // colour swatch
      p.circ(8, 8, 7.2f);
      p.circ(8, 8, 5.6f, true);
      p.circ(8, 8, 4.4f);
      break;

    case IC_PENCIL:
      p.line(4.2f, 11.8f, 12.2f, 3.8f, 3.4f);
      p.line(10.4f, 5.6f, 11.4f, 6.6f, 0.9f, true);  // ferrule line
      p.tri(1.4f, 14.6f, 2.6f, 10.6f, 5.4f, 13.4f);
      break;

    case IC_STAR:  // five-point star in a ring (life points)
      p.arc(8, 8, 6.4f, 7.6f, 0, 360);
      p.star(8, 8.4f, 5.6f, 2.3f, 5);
      break;

    case IC_LORE:  // faceted gem
      p.tri(8, 1, 1.5f, 6, 14.5f, 6);
      p.tri(1.5f, 6, 14.5f, 6, 8, 15);
      p.line(1.5f, 6, 14.5f, 6, 0.9f, true);
      p.line(5.2f, 6, 8, 14.2f, 0.8f, true);
      p.line(10.8f, 6, 8, 14.2f, 0.8f, true);
      break;

    case IC_PRIZE:  // face-down card with a star
      p.rrect(3.2f, 1.2f, 9.6f, 13.6f, 1.6f);
      p.rrect(4.6f, 2.6f, 6.8f, 10.8f, 1, true);
      p.star(8, 8.2f, 3.2f, 1.3f, 5);
      break;

    case IC_TROPHY:  // cup on a stand
      p.rect(4, 1.6f, 8, 5.6f);
      p.circ(8, 7.2f, 4);
      p.arc(3.6f, 5.4f, 1.6f, 2.8f, 90, 270);
      p.arc(12.4f, 5.4f, 1.6f, 2.8f, 270, 450);
      p.rect(7.1f, 10.6f, 1.8f, 2.4f);
      p.rrect(4.4f, 12.8f, 7.2f, 2.2f, 0.8f);
      break;

    case IC_MENU:  // three bars
      p.rrect(2, 2.8f, 12, 2.2f, 1.1f);
      p.rrect(2, 6.9f, 12, 2.2f, 1.1f);
      p.rrect(2, 11, 12, 2.2f, 1.1f);
      break;
  }
}
