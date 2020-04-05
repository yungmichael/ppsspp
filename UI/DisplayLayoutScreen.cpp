// Copyright (c) 2013- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <vector>

#include "base/colorutil.h"
#include "base/display.h"
#include "gfx/texture_atlas.h"
#include "gfx_es2/draw_buffer.h"
#include "i18n/i18n.h"
#include "ui/ui_context.h"
#include "ui/view.h"

#include "DisplayLayoutScreen.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "DisplayLayoutEditor.h"
#include "GPU/Common/FramebufferCommon.h"

static const int leftColumnWidth = 200;
static const float orgRatio = 1.764706f;

static float ScaleSettingToUI() {
	float scale = g_Config.fSmallDisplayZoomLevel * 8.0f;
	// Account for 1x display doubling dps.
	if (g_dpi_scale_x > 1.0f) {
		scale *= g_dpi_scale_x;
	}
	return scale;
}

static void UpdateScaleSetting(float scale) {
	// Account for 1x display doubling dps.
	if (g_dpi_scale_x > 1.0f) {
		scale /= g_dpi_scale_x;
	}
	g_Config.fSmallDisplayZoomLevel = scale;
}

static void UpdateScaleSettingFromUI(float scale) {
	UpdateScaleSetting(scale / 8.0f);
}

class DragDropDisplay : public MultiTouchDisplay {
public:
	DragDropDisplay(float &x, float &y, ImageID img, float &scale, const Bounds &screenBounds)
		: MultiTouchDisplay(img, scale, new UI::AnchorLayoutParams(x * screenBounds.w, y * screenBounds.h, UI::NONE, UI::NONE, true)),
		x_(x), y_(y), theScale_(scale), screenBounds_(screenBounds) {
		scale_ = theScale_;
	}	

	virtual void SaveDisplayPosition() {
		x_ = bounds_.centerX() / screenBounds_.w;
		y_ = bounds_.centerY() / screenBounds_.h;
		scale_ = theScale_;
	}

	virtual float GetScale() const { return theScale_; }
	virtual void SetScale(float s) { theScale_ = s; scale_ = s; }

private:
	float &x_, &y_;
	float &theScale_;
	const Bounds &screenBounds_;
};

DisplayLayoutScreen::DisplayLayoutScreen() {
	picked_ = nullptr;
	mode_ = nullptr;
};

bool DisplayLayoutScreen::touch(const TouchInput &touch) {
	UIScreen::touch(touch);

	using namespace UI;

	int mode = mode_ ? mode_->GetSelection() : 0;
	if (g_Config.iSmallDisplayZoomType == (int)SmallDisplayZoom::AUTO) {
		mode = -1;
	}

	const Bounds &screen_bounds = screenManager()->getUIContext()->GetBounds();
	if ((touch.flags & TOUCH_MOVE) && picked_ != nullptr) {
		int touchX = touch.x - offsetTouchX;
		int touchY = touch.y - offsetTouchY;
		if (mode == 0) {
			const auto &prevParams = picked_->GetLayoutParams()->As<AnchorLayoutParams>();
			Point newPos(prevParams->left, prevParams->top);

			int limitX = g_Config.fSmallDisplayZoomLevel * 120;
			int limitY = g_Config.fSmallDisplayZoomLevel * 68;

			const int quarterResX = screen_bounds.w / 4;
			const int quarterResY = screen_bounds.h / 4;

			if (bRotated) {
				//swap X/Y limit for rotated display
				int limitTemp = limitX;
				limitX = limitY;
				limitY = limitTemp;
			}

			// Check where each edge of the screen is
			const int windowLeftEdge = quarterResX;
			const int windowRightEdge = windowLeftEdge * 3;
			const int windowUpperEdge = quarterResY;
			const int windowLowerEdge = windowUpperEdge * 3;
			// And stick display when close to any edge
			stickToEdgeX = false; stickToEdgeY = false;
			if (touchX > windowLeftEdge - 8 + limitX && touchX < windowLeftEdge + 8 + limitX) { touchX = windowLeftEdge + limitX; stickToEdgeX = true; }
			if (touchX > windowRightEdge - 8 - limitX && touchX < windowRightEdge + 8 - limitX) { touchX = windowRightEdge - limitX; stickToEdgeX = true; }
			if (touchY > windowUpperEdge - 8 + limitY && touchY < windowUpperEdge + 8 + limitY) { touchY = windowUpperEdge + limitY; stickToEdgeY = true; }
			if (touchY > windowLowerEdge - 8 - limitY && touchY < windowLowerEdge + 8 - limitY) { touchY = windowLowerEdge - limitY; stickToEdgeY = true; }

			const int minX = screen_bounds.w / 2;
			const int maxX = screen_bounds.w + minX;
			const int minY = screen_bounds.h / 2;
			const int maxY = screen_bounds.h + minY;
			// Display visualization disappear outside of those bounds, so we have to limit
			if (touchX < -minX) touchX = -minX;
			if (touchX >  maxX) touchX =  maxX;
			if (touchY < -minY) touchY = -minY;
			if (touchY >  maxY) touchY =  maxY;

			// Limit small display on much larger output a bit differently
			if (quarterResX > limitX) limitX = quarterResX;
			if (quarterResY > limitY) limitY = quarterResY;

			// Allow moving zoomed in display freely as long as at least noticeable portion of the screen is occupied
			if (touchX > minX - limitX - 10 && touchX < minX + limitX + 10) {
				newPos.x = touchX;
			}
			if (touchY > minY - limitY - 10 && touchY < minY + limitY + 10) {
				newPos.y = touchY;
			}
			picked_->ReplaceLayoutParams(new AnchorLayoutParams(newPos.x, newPos.y, NONE, NONE, true));
		} else if (mode == 1) {
			// Resize. Vertical = scaling, horizontal = spacing;
			// Up should be bigger so let's negate in that direction
			float diffX = (touchX - startX_);
			float diffY = -(touchY - startY_);

			float movementScale = 0.5f;
			float newScale = startScale_ + diffY * movementScale;
			// Desired scale * 8.0 since the visualization is tiny size and multiplied by 8.
			if (newScale > 80.0f) newScale = 80.0f;
			if (newScale < 8.0f) newScale = 8.0f;
			picked_->SetScale(newScale);
			scaleUpdate_ = picked_->GetScale();
			UpdateScaleSettingFromUI(scaleUpdate_);
		}
	}
	if ((touch.flags & TOUCH_DOWN) && picked_ == 0) {
		picked_ = displayRepresentation_;
		if (picked_) {
			const Bounds &bounds = picked_->GetBounds();
			startX_ = bounds.centerX();
			startY_ = bounds.centerY();
			offsetTouchX = touch.x - startX_;
			offsetTouchY = touch.y - startY_;
			startScale_ = picked_->GetScale();
		}
	}
	if ((touch.flags & TOUCH_UP) && picked_ != 0) {
		const Bounds &bounds = picked_->GetBounds();
		startScale_ = picked_->GetScale();
		picked_->SaveDisplayPosition();
		picked_ = nullptr;
	}
	return true;
}

void DisplayLayoutScreen::resized() {
	RecreateViews();
}

void DisplayLayoutScreen::onFinish(DialogResult reason) {
	g_Config.Save("DisplayLayoutScreen::onFinish");
}

UI::EventReturn DisplayLayoutScreen::OnCenter(UI::EventParams &e) {
	if (!stickToEdgeX || (stickToEdgeX && stickToEdgeY))
		g_Config.fSmallDisplayOffsetX = 0.5f;
	if (!stickToEdgeY || (stickToEdgeX && stickToEdgeY))
		g_Config.fSmallDisplayOffsetY = 0.5f;
	RecreateViews();
	return UI::EVENT_DONE;
};

UI::EventReturn DisplayLayoutScreen::OnZoomTypeChange(UI::EventParams &e) {
	if (g_Config.iSmallDisplayZoomType < (int)SmallDisplayZoom::MANUAL) {
		const Bounds &bounds = screenManager()->getUIContext()->GetBounds();
		float autoBound = bounds.w / 480.0f;
		UpdateScaleSetting(autoBound);
		displayRepresentationScale_ = ScaleSettingToUI();
		g_Config.fSmallDisplayOffsetX = 0.5f;
		g_Config.fSmallDisplayOffsetY = 0.5f;
	}
	RecreateViews();
	return UI::EVENT_DONE;
};

void DisplayLayoutScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	RecreateViews();
}

class Boundary : public UI::View {
public:
	Boundary(UI::LayoutParams *layoutParams) : UI::View(layoutParams) {
	}

	void Draw(UIContext &dc) override {
		dc.Draw()->DrawImageStretch(dc.theme->whiteImage, bounds_.x, bounds_.y, bounds_.x2(), bounds_.y2(), dc.theme->itemDownStyle.background.color);
	}
};

// Stealing StickyChoice's layout and text rendering.
class HighlightLabel : public UI::StickyChoice {
public:
	HighlightLabel(const std::string &text, UI::LayoutParams *layoutParams)
		: UI::StickyChoice(text, "", layoutParams) {
		Press();
	}

	bool CanBeFocused() const override { return false; }
};

void DisplayLayoutScreen::CreateViews() {
	const Bounds &bounds = screenManager()->getUIContext()->GetBounds();

	using namespace UI;

	auto di = GetI18NCategory("Dialog");
	auto gr = GetI18NCategory("Graphics");
	auto co = GetI18NCategory("Controls");

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	const float previewWidth = bounds.w / 2.0f;
	const float previewHeight = bounds.h / 2.0f;

	// Just visual boundaries of the screen, should be easier to use than imagination
	const float horizPreviewPadding = bounds.w / 4.0f;
	const float vertPreviewPadding = bounds.h / 4.0f;
	const float horizBoundariesWidth = 4.0f;
	// This makes it have at least 10.0f padding below at 1x.
	const float vertBoundariesHeight = 52.0f;

	// Left side, right, top, bottom.
	root_->Add(new Boundary(new AnchorLayoutParams(horizBoundariesWidth, FILL_PARENT, NONE, 0, horizPreviewPadding + previewWidth, 0)));
	root_->Add(new Boundary(new AnchorLayoutParams(horizBoundariesWidth, FILL_PARENT, horizPreviewPadding + previewWidth, 0, NONE, 0)));
	root_->Add(new Boundary(new AnchorLayoutParams(previewWidth, vertBoundariesHeight, horizPreviewPadding, vertPreviewPadding - vertBoundariesHeight, NONE, NONE)));
	root_->Add(new Boundary(new AnchorLayoutParams(previewWidth, vertBoundariesHeight, horizPreviewPadding, NONE, NONE, vertPreviewPadding - vertBoundariesHeight)));

	static const char *zoomLevels[] = { "Stretching", "Partial Stretch", "Auto Scaling", "Manual Scaling" };
	zoom_ = new PopupMultiChoice(&g_Config.iSmallDisplayZoomType, di->T("Options"), zoomLevels, 0, ARRAY_SIZE(zoomLevels), gr->GetName(), screenManager(), new AnchorLayoutParams(400, WRAP_CONTENT, previewWidth - 200.0f, NONE, NONE, 10));
	zoom_->OnChoice.Handle(this, &DisplayLayoutScreen::OnZoomTypeChange);

	static const char *displayRotation[] = { "Landscape", "Portrait", "Landscape Reversed", "Portrait Reversed" };
	rotation_ = new PopupMultiChoice(&g_Config.iInternalScreenRotation, gr->T("Rotation"), displayRotation, 1, ARRAY_SIZE(displayRotation), co->GetName(), screenManager(), new AnchorLayoutParams(400, WRAP_CONTENT, previewWidth - 200.0f, 10, NONE, bounds.h - 64 - 10));
	rotation_->SetEnabledFunc([] {
		return g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
	});
	bool displayRotEnable = (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE);
	bRotated = false;
	if (displayRotEnable && (g_Config.iInternalScreenRotation == ROTATION_LOCKED_VERTICAL || g_Config.iInternalScreenRotation == ROTATION_LOCKED_VERTICAL180)) {
		bRotated = true;
	}
	// Visual representation image is just icon size and have to be scaled 8 times to match PSP native resolution which is used as 1.0 for zoom
	displayRepresentationScale_ = ScaleSettingToUI();

	HighlightLabel *label = nullptr;
	mode_ = nullptr;
	if (g_Config.iSmallDisplayZoomType >= (int)SmallDisplayZoom::AUTO) { // Scaling
		if (g_Config.iSmallDisplayZoomType == (int)SmallDisplayZoom::AUTO) {
			label = new HighlightLabel(gr->T("Auto Scaling"), new AnchorLayoutParams(WRAP_CONTENT, 64.0f, bounds.w / 2.0f, bounds.h / 2.0f, NONE, NONE, true));
			float autoBound = bounds.h / 270.0f;
			// Case of screen rotated ~ only works with buffered rendering
			if (bRotated) {
				autoBound = bounds.h / 480.0f;
			} else { // Without rotation in common cases like 1080p we cut off 2 pixels of height, this reflects other cases
				float resCommonWidescreen = autoBound - floor(autoBound);
				if (resCommonWidescreen != 0.0f) {
					float ratio = bounds.w / bounds.h;
					if (ratio < orgRatio) {
						autoBound = bounds.w / 480.0f;
					}
					else {
						autoBound = bounds.h / 272.0f;
					}
				}
			}
			UpdateScaleSetting(autoBound);
			displayRepresentationScale_ = ScaleSettingToUI();
			g_Config.fSmallDisplayOffsetX = 0.5f;
			g_Config.fSmallDisplayOffsetY = 0.5f;
		} else { // Manual Scaling
			Choice *center = new Choice(di->T("Center"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 74));
			center->OnClick.Handle(this, &DisplayLayoutScreen::OnCenter);
			root_->Add(center);
			float minZoom = 1.0f;
			if (g_dpi_scale_x > 1.0f) {
				minZoom /= g_dpi_scale_x;
			}
			PopupSliderChoiceFloat *zoomlvl = new PopupSliderChoiceFloat(&g_Config.fSmallDisplayZoomLevel, minZoom, 10.0f, di->T("Zoom"), 1.0f, screenManager(), di->T("* PSP res"), new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 10 + 64 + 64));
			root_->Add(zoomlvl);
			mode_ = new ChoiceStrip(ORIENT_VERTICAL, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 158 + 64 + 10));
			mode_->AddChoice(di->T("Move"));
			mode_->AddChoice(di->T("Resize"));
			mode_->SetSelection(0);
		}
		displayRepresentation_ = new DragDropDisplay(g_Config.fSmallDisplayOffsetX, g_Config.fSmallDisplayOffsetY, ImageID("I_PSP_DISPLAY"), displayRepresentationScale_, bounds);
		displayRepresentation_->SetVisibility(V_VISIBLE);
	} else { // Stretching
		label = new HighlightLabel(gr->T("Stretching"), new AnchorLayoutParams(WRAP_CONTENT, 64.0f, bounds.w / 2.0f, bounds.h / 2.0f, NONE, NONE, true));
		displayRepresentation_ = new DragDropDisplay(g_Config.fSmallDisplayOffsetX, g_Config.fSmallDisplayOffsetY, ImageID("I_PSP_DISPLAY"), displayRepresentationScale_, bounds);
		displayRepresentation_->SetVisibility(V_INVISIBLE);
		float width = previewWidth;
		float height = previewHeight;
		if (g_Config.iSmallDisplayZoomType == (int)SmallDisplayZoom::STRETCH) {
			Choice *stretched = new Choice("", "", false, new AnchorLayoutParams(width, height, width - width / 2.0f, NONE, NONE, height - height / 2.0f));
			stretched->SetEnabled(false);
			root_->Add(stretched);
		} else { // Partially stretched
			float origRatio = !bRotated ? 480.0f / 272.0f : 272.0f / 480.0f;
			float frameRatio = width / height;
			if (origRatio > frameRatio) {
				height = width / origRatio;
				if (!bRotated && g_Config.iSmallDisplayZoomType == (int)SmallDisplayZoom::PARTIAL_STRETCH) {
					height = (272.0f + height) / 2.0f;
				}
			} else {
				width = height * origRatio;
				if (bRotated && g_Config.iSmallDisplayZoomType == (int)SmallDisplayZoom::PARTIAL_STRETCH) {
					width = (272.0f + height) / 2.0f;
				}
			}
			Choice *stretched = new Choice("", "", false, new AnchorLayoutParams(width, height, previewWidth - width / 2.0f, NONE, NONE, previewHeight - height / 2.0f));
			stretched->SetEnabled(false);
			root_->Add(stretched);
		}
	}
	if (bRotated) {
		displayRepresentation_->SetAngle(90.0f);
	}

	Choice *back = new Choice(di->T("Back"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 10));
	back->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	root_->Add(displayRepresentation_);
	if (mode_) {
		root_->Add(mode_);
	}
	if (label) {
		root_->Add(label);
	}
	root_->Add(zoom_);
	root_->Add(rotation_);
	root_->Add(back);
}
