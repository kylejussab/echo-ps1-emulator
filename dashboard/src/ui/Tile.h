#pragma once

#include <SDL3/SDL.h>

class Tile {
    public:
        Tile(float x_, float y_, float width_ = 40.0f, float height_ = 40.0f, SDL_FColor color_ = {0.85f, 0.85f, 0.85f, 1.0f});


        virtual ~Tile() = default;


        Tile* setInteractable(bool interactable_) { interactable = interactable_; return this; }


        Tile* setPosition(float x_, float y_) { x = x_; y = y_; return this; }


        Tile* setSize(float width_, float height_) { width = width_; height = height_; return this; }


        Tile* setBackgroundColor(SDL_FColor color_) { color = color_; return this; }


        Tile* setCornerRadius(float radius = 0.0f) { cornerRadius = radius; return this; }


        Tile* setBorder(float width = 0.0f) { borderWidth = width; return this; }


        Tile* setBorderColor(SDL_FColor color_) { borderColor = color_; return this; }


        Tile* setPadding(float left = 0.0f, float right = 0.0f, float top = 0.0f, float bottom = 0.0f) {
            paddingLeft = left; paddingRight = right; paddingTop = top; paddingBottom = bottom;
            return this;
        }

        bool isInteractable() const { return interactable; }

        // Finishes a chain. Plain tiles have nothing to bake; subclasses override.
        virtual void build(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd) {}

        // Tiles that bake their content into a pixel-sized texture (text, icons) override this.
        // Dashboard calls it with the new scale, then calls build() to re-rasterize.
        virtual void setRasterScale(float scale) {}

        virtual void Draw(SDL_GPUCommandBuffer* cmdBuffer, SDL_GPURenderPass* renderPass, float pixelsPerUnit, float designWidth = 1920.0f, float designHeight = 1080.0f) const;

        virtual void update(float deltaSeconds) {}


        virtual void setFocused(bool focused_) { focused = focused_; }
        bool isFocused() const { return focused; }

        float getCenterX() const { return x + width / 2.0f; }

        float getCornerRadius() const { return cornerRadius; }


        // Hidden tiles are skipped when drawing
        void setVisible(bool visible_) { visible = visible_; }
        bool isVisible() const { return visible; }


        // Multiplies the tile's alpha when it is drawn. Used to fade whole groups of tiles in and out.
        Tile* setDrawOpacity(float opacity_) { drawOpacity = opacity_; return this; }
        float getDrawOpacity() const { return drawOpacity; }


		// Clipped tiles are cut off above the content area, so scrolled content slides under the navigation
		Tile* setClippedToContent(bool clipped_) { clippedToContent = clipped_; return this; }
		bool isClippedToContent() const { return clippedToContent; }


        float getWidth() const { return width; }
	    float getHeight() const { return height; }

        float getX() const { return x; }
	    float getY() const { return y; }


    protected:
        bool interactable = true;
        bool visible = true;
		bool clippedToContent = false;
        float drawOpacity = 1.0f;

        float x;
        float y;
        float width;
        float height;
        SDL_FColor color;
        SDL_FColor borderColor = {0.0f, 0.0f, 0.0f, 0.0f};
        float cornerRadius = 0.0f;
        float borderWidth = 0.0f;
        float paddingLeft = 0.0f;
        float paddingRight = 0.0f;
        float paddingTop = 0.0f;
        float paddingBottom = 0.0f;

        bool focused = false;
        static constexpr float focusBorderWidth = 2.0f;
};