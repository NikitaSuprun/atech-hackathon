#include "score_display.h"
#include <stdio.h>

// RGB565 mid-gray for secondary text
static constexpr uint16_t COLOR_GRAY = 0x8410;
static constexpr uint16_t P1_COLOR = ST7735_TFT::COLOR_CYAN;
static constexpr uint16_t P2_COLOR = ST7735_TFT::COLOR_ORANGE;

// score line: P1 digit / colon / P2 digit at fixed columns 44/68/92
static void scoreLine(ST7735_TFT* tft, uint8_t s0, uint8_t s1, int16_t y, uint8_t size) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%u", (unsigned)s0);
    tft->displayText(buf, 44, y, size, P1_COLOR);
    tft->displayText(":", 68, y, size, ST7735_TFT::COLOR_WHITE);
    snprintf(buf, sizeof(buf), "%u", (unsigned)s1);
    tft->displayText(buf, 92, y, size, P2_COLOR);
}

// 60x12 hold bar; q is readyProgress/16 (0..15); full bar snaps to 60 px + READY
static void holdBar(ST7735_TFT* tft, int16_t x, int16_t y, uint8_t q, uint16_t color) {
    tft->drawRect(x, y, 60, 12, color);
    int16_t w = (q >= 15) ? 60 : (int16_t)(q * 60 / 16);
    if (w > 0)
        tft->fillRect(x, y, w, 12, color);
    if (q >= 15)
        tft->displayText("READY", x + 14, y + 2, 1, ST7735_TFT::COLOR_BLACK);
}

void ScoreDisplay::begin(ST7735_TFT* tft) {
    tft_ = tft;
    // driver boots in rotation 3 (160x80 landscape); re-assert defensively
    tft_->setRotation(3);
    tft_->clear();
    tft_->displayText("PONG", 44, 10, 3, ST7735_TFT::COLOR_WHITE);
    tft_->displayText("connecting...", 40, 50, 1, COLOR_GRAY);
    tft_->display();
}

void ScoreDisplay::apply(const PongFeedbackPacket& snap, const PongCues& cues, bool linked) {
    (void)cues;
    goalBy_ = snap.goalBy;
    Model m;
    m.state = snap.gameState;
    m.s0 = snap.score[0];
    m.s1 = snap.score[1];
    m.held = snap.heldBits;
    m.hold0q = (uint8_t)(snap.readyProgress[0] / 16);
    m.hold1q = (uint8_t)(snap.readyProgress[1] / 16);
    m.winner = snap.winner;
    m.linked = linked;
    if (m != last_) {
        draw(m);
        last_ = m;
    }
}

void ScoreDisplay::draw(const Model& m) {
    if (!tft_)
        return;
    tft_->clear();

    if (!m.linked) {
        tft_->displayText("SEARCHING WALL...", 29, 36, 1, ST7735_TFT::COLOR_YELLOW);
        if (m.s0 || m.s1) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%u:%u", (unsigned)m.s0, (unsigned)m.s1);
            tft_->displayText(buf, 2, 2, 1, ST7735_TFT::COLOR_WHITE);
        }
        tft_->display();
        return;
    }

    switch (m.state) {
    case GS_LINK_WAIT:
        scoreLine(tft_, m.s0, m.s1, 8, 4);
        tft_->displayText("WALL WAITING FOR INPUT", 14, 60, 1, ST7735_TFT::COLOR_WHITE);
        break;

    case GS_READY_CHECK:
        if (m.s0 == 0 && m.s1 == 0 && m.winner == PONG_NOBODY) {
            tft_->displayText("HOLD TO START", 41, 6, 1, ST7735_TFT::COLOR_WHITE);
            holdBar(tft_, 10, 30, m.hold0q, P1_COLOR);
            holdBar(tft_, 90, 30, m.hold1q, P2_COLOR);
            tft_->displayText("P1", 34, 46, 1, P1_COLOR);
            tft_->displayText("P2", 114, 46, 1, P2_COLOR);
        } else {
            scoreLine(tft_, m.s0, m.s1, 8, 4);
            tft_->displayText("HOLD FOR NEXT POINT", 23, 42, 1, ST7735_TFT::COLOR_WHITE);
            holdBar(tft_, 10, 56, m.hold0q, P1_COLOR);
            holdBar(tft_, 90, 56, m.hold1q, P2_COLOR);
        }
        break;

    case GS_COUNTDOWN:
        scoreLine(tft_, m.s0, m.s1, 8, 4);
        tft_->displayText("GET READY", 53, 48, 1, ST7735_TFT::COLOR_WHITE);
        tft_->displayText("SERVE!", 56, 60, 1, ST7735_TFT::COLOR_WHITE);
        break;

    case GS_PLAYING:
        scoreLine(tft_, m.s0, m.s1, 8, 4);
        tft_->displayText("RALLY", 62, 60, 1, ST7735_TFT::COLOR_WHITE);
        break;

    case GS_POINT_FLASH: {
        const bool p2 = (goalBy_ == 1);
        tft_->fillScreen(p2 ? P2_COLOR : P1_COLOR);
        tft_->displayText(p2 ? "POINT P2!" : "POINT P1!", 30, 32, 2, ST7735_TFT::COLOR_BLACK);
        break;
    }

    case GS_GAME_OVER: {
        const bool p2 = (m.winner == 1);
        tft_->displayText(p2 ? "P2 WINS" : "P1 WINS", 17, 12, 3, p2 ? P2_COLOR : P1_COLOR);
        scoreLine(tft_, m.s0, m.s1, 44, 3);
        tft_->displayText("HOLD FOR REMATCH", 32, 70, 1, ST7735_TFT::COLOR_WHITE);
        break;
    }

    default:
        scoreLine(tft_, m.s0, m.s1, 8, 4);
        break;
    }

    tft_->display();
}
