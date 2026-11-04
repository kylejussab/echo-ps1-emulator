#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <vector>
#include <memory>
#include <string>
#include "ui/Tile.h"
#include "input/ControllerInput.h"
#include "app/Preferences.h"
#include "app/GameAudio.h"
#include "gfx/Renderer.h"
#include "gfx/GameView.h"
#include "library/GameLibrary.h"
#include "ui/CollectionGrid.h"
#include "ui/RecentlyPlayedRow.h"
#include "ui/EmptyState.h"
#include "ui/TabFader.h"
#include "app/FocusManager.h"
#include "ui/NavBar.h"
#include "ui/WidgetRow.h"
#include <functional>
#include "app/UserData.h"
#include "ui/MenuTile.h"
#include "ui/GameBackground.h"

class PlayStation;

class Dashboard {
    public:
        Dashboard();
        ~Dashboard();

        bool initialize();
        void run();

    private:
        // What the app is doing right now. The game itself only exists in GameRunning and GameIdle.
        enum class AppState {
            Dashboard,   // no game loaded, the dashboard is on screen
            Launching,   // the picked tile is growing to the middle of the screen, then fading to black; the game starts when it is black
            GameRunning, // the game owns the screen, the emulator runs every frame
            GameIdle     // a game is loaded but paused behind the dashboard: runFrame() isn't called and its audio is paused
        };

        ControllerInput m_gamepad;
        Renderer m_renderer;
        GameView m_gameView{ m_renderer };
        GameLibrary m_library;
        EmptyState m_emptyState{ m_renderer };
        RecentlyPlayedRow m_recentlyPlayedRow{ m_renderer };
        CollectionGrid m_collectionGrid{ m_renderer };
        TabFader m_tabs;
        FocusManager m_focus;
        NavBar m_navBar{ m_renderer };
        WidgetRow m_widgetRow{ m_renderer };
        
        void buildUI();

        bool m_isRunning = false;
        Uint64 m_lastFrameNs = 0;

        void rebuildRasterTiles();

        float m_rasterScale = 1.0f;
        Uint64 m_rasterRebuildAt = 0;

        std::unique_ptr<GameBackground> m_background; // created in buildUI(), because it needs the renderer's pipelines

        // One event loop for everything. Decides where each event goes based on m_state.
        void handleEvent(const SDL_Event& event);

        // One frame of each kind of screen
        void stepDashboard();
        void stepGame();

        void navigate(NavInput nav);

        void setActiveTab(Tab tab);
        void applyTabVisibility();

        Preferences m_prefs;
        UserData m_userData;

        void rebuildLibrary();
        void handleDroppedPath(const char* dropped);

        // The game, in this process. Creates the console, powers it on and gives it the screen.
        bool startGame(const std::string& cuePath);

        // PS button: game <-> dashboard
        void toggleGame();
        void pauseGame();
        void resumeGame();

        // Powers the console off and goes back to the plain dashboard
        void stopGame();

        AppState m_state = AppState::Dashboard;
        std::unique_ptr<PlayStation> m_game; // null in the Dashboard state
        std::string m_gameCuePath;           // which disc m_game is running
        GameAudio m_audio;
        std::vector<int16_t> m_audioScratch;


        // Options menu. Created in buildUI() because it needs the renderer's pipelines.
		std::unique_ptr<MenuTile> m_optionsMenu;
		bool m_optionsMenuOpen = false;
		std::string m_optionsGameId; // the game the menu was opened on
        GameTile* m_optionsTile = nullptr; // the tile the menu was opened on
		void runOptionsAction(MenuAction action, int argument, GameTile* tile);

		std::vector<MenuRowInfo> optionsMenuRows(const GameInfo& info) const;
		void openOptionsMenu(GameTile* tile);
		void closeOptionsMenu();
		void navigateOptionsMenu(NavInput nav);
		void buildOptionsMenu();



        // Launch animation
		void beginLaunch(const std::string& cuePath, GameTile* tile);
		void updateLaunch(float deltaSeconds);
		void drawLaunch(Renderer::Frame& frame);
		void finishLaunch();

		GameTile* m_launchTile = nullptr;
		std::string m_launchCuePath;
		SDL_FRect m_launchStartRect{};
		bool m_launchWasClipped = false;
		float m_launchSeconds = 0.0f;
		Tile m_launchDim{ 0.0f, 0.0f, Renderer::designWidth, Renderer::designHeight, Renderer::defaultClearColor };

		// A fade to black and back. When the screen is fully black, the callback runs; that is where the real switch happens.
		enum class FadePhase { None, Out, In };
		void beginFade(std::function<void()> atBlack);
		void updateFade(float deltaSeconds);

		FadePhase m_fadePhase = FadePhase::None;
		float m_fadeSeconds = 0.0f;
		std::function<void()> m_fadeAtBlack;
		Tile m_fadeOverlay{ 0.0f, 0.0f, Renderer::designWidth, Renderer::designHeight, SDL_FColor{ 0.0f, 0.0f, 0.0f, 1.0f } };

        float m_launchStartCornerRadius = 0.0f;
		bool m_launchWasFocused = false;

        void applyPlayHistory();
		void recordPlayed(const std::string& gameId);

        void commitPlaytime();

		std::string m_playingGameId;     // the game that is loaded, empty when none is
		double m_pendingPlaySeconds = 0.0; // played, but not yet added to the saved total
};
