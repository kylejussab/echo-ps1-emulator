#include "app/Dashboard.h"
#include <cstring>
#include <iostream>
#include <string>
#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include "core/Constants.h"
#include "core/PlayStation.h"
#include "ui/Layout.h"

using namespace layout;


// Holding left/right on the d-pad or stick
static constexpr float navRepeatDelaySeconds    = 0.35f; // hold this long before it starts scrolling
static constexpr float navRepeatIntervalSeconds = 0.06f; // time per tile while scrolling (smaller = faster)

// The bars around a game's 4:3 picture are black. The dashboard uses Renderer::defaultClearColor.
static constexpr SDL_FColor gameClearColor{ 0.0f, 0.0f, 0.0f, 1.0f };

static constexpr float launchGrowSeconds = 0.45f;
static constexpr float launchEndSize = Renderer::designHeight; // the tile ends up a square this big, centred
static constexpr float fadeOutSeconds = 0.25f;
static constexpr float fadeInSeconds = 0.25f;

static constexpr float launchFadeStartFraction = 0.4f; // how far through the grow the black fade begins (1.0 = only after it lands)

static constexpr const char* playHistoryKeyPrefix = "lastPlayed.";
static constexpr const char* playtimeKeyPrefix = "playtime.";
static constexpr double longestCountedFrameSeconds = 0.25; // a longer gap between frames was a stall (like the console starting up), not play
static constexpr double playtimeAutosaveSeconds = 60.0;

static float interpolate(float from, float to, float progress) {
	return from + (to - from) * progress;
}

static float easeInOutCubic(float progress) {
	return progress < 0.5f ? 4.0f * progress * progress * progress
	                       : 1.0f - std::pow(-2.0f * progress + 2.0f, 3.0f) / 2.0f;
}


// The PS button. SDL calls it the Guide button.
static bool isGuideButtonEvent(const SDL_Event& event) {
	return (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP)
	    && event.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE;
}



Dashboard::Dashboard() {
    m_gamepad.setRepeat(navRepeatDelaySeconds, navRepeatIntervalSeconds);
}


Dashboard::~Dashboard() {
	// The console, its audio device and its controller all use SDL, so they have to go before SDL_Quit below
	commitPlaytime();
	m_audio.close();
	m_game.reset();

	m_gamepad.close();

	// The tiles and the game view hold GPU resources, so they go before the renderer does
	if (m_renderer.device()) SDL_WaitForGPUIdle(m_renderer.device());
	m_gameView.release();
	m_navBar.clear();
	m_widgetRow.clear();
	m_collectionGrid.clear();
	m_recentlyPlayedRow.clear();
	m_emptyState.clear();
	m_optionsMenu.reset();
	m_background.reset();

	m_renderer.shutdown();

	TTF_Quit();
	SDL_Quit();
}


bool Dashboard::initialize() {
	if (!TTF_Init()) {
		std::cout << "TTF_Init failed: " << SDL_GetError() << std::endl;
		return false;
	}

	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ENHANCED_REPORTS, "1");

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
		std::cout << "SDL_Init failed: " << SDL_GetError() << std::endl;
		return false;
	}

	if (!m_renderer.initialize("Echo", 1280, 720)) return false;

	if (!m_gameView.build()) {
		std::cout << "Could not create the game view: " << SDL_GetError() << std::endl;
		return false;
	}

	if (const char* basePath = SDL_GetBasePath()) {
		m_library.loadDatabase(std::string(basePath) + "data/games.db",
		                       std::string(basePath) + "boxart");
	}

	m_prefs.load();
	m_userData.load();
	m_library.scan(m_prefs.getString("gamesFolder"));

	buildUI();

	return true;
}


void Dashboard::run() {
	m_isRunning = true;
	m_lastFrameNs = SDL_GetTicksNS();

	while (m_isRunning){
		SDL_Event event;
		while (SDL_PollEvent(&event)){
			handleEvent(event);
		}

		if (m_state == AppState::GameRunning) stepGame();
		else stepDashboard();
	}
}


void Dashboard::handleEvent(const SDL_Event& event) {
	if (event.type == SDL_EVENT_QUIT) {
		m_isRunning = false;
		return;
	}

	// The PS button belongs to Echo: it is caught before anything else and never reaches the emulated controller or the dashboard navigation.
	// Both the press and the release are swallowed. (The Select + Start hold still toggles the emulated controller's analog mode.)
	if (isGuideButtonEvent(event)) {
		if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) toggleGame();
		return;
	}

	if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
		// F1 does what the PS button does. It's the fallback for controllers (or macOS) that don't deliver the PS button.
		if (event.key.key == SDLK_F1) {
			toggleGame();
			return;
		}
	}

	const bool isHotplug = (event.type == SDL_EVENT_GAMEPAD_ADDED || event.type == SDL_EVENT_GAMEPAD_REMOVED);

	// The game gets every event while it runs. While it idles behind the dashboard it only hears about controllers
	// being plugged in or out, so it doesn't miss a replug.
	if (m_game && (m_state == AppState::GameRunning || isHotplug)) {
		m_game->handleEvent(event);
	}

	if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED){
		m_rasterRebuildAt = SDL_GetTicks() + 150;
	}
	else if (isHotplug){
		m_widgetRow.requestControllerUpdate();
	}
	else if (event.type == SDL_EVENT_DROP_FILE){
		handleDroppedPath(event.drop.data);
	}

	// The dashboard's controller input tracks plugging and unplugging all the time, but only reacts to presses while the dashboard is on screen
	if (m_state != AppState::GameRunning || isHotplug) {
		const NavInput padInput = m_gamepad.handleEvent(event);
		if (m_state != AppState::GameRunning) navigate(padInput);
	}
}


void Dashboard::stepDashboard() {
	if (m_rasterRebuildAt != 0 && SDL_GetTicks() >= m_rasterRebuildAt){
		m_rasterRebuildAt = 0;
		rebuildRasterTiles();
	}

	// Time since the last frame, capped so a stall (like dragging the window) doesn't cause a huge jump
	const Uint64 nowNs = SDL_GetTicksNS();
	const float deltaSeconds = std::min(static_cast<float>(nowNs - m_lastFrameNs) / 1.0e9f, 0.05f);
	m_lastFrameNs = nowNs;

	navigate(m_gamepad.update(deltaSeconds));
	updateLaunch(deltaSeconds);

	m_tabs.update(deltaSeconds);
	applyTabVisibility();

	const GameInfo* featuredGame = m_recentlyPlayedRow.selectedGame();
	m_background->setGame(featuredGame ? featuredGame->id : std::string());
	m_background->update(deltaSeconds);

	m_navBar.update(deltaSeconds);
	m_widgetRow.update(deltaSeconds);

	m_collectionGrid.update(deltaSeconds);
	
	// The featured game is suspended when it is the loaded game and the console is paused behind the dashboard
	const bool isFeaturedGameSuspended = m_state == AppState::GameIdle && featuredGame && featuredGame->id == m_playingGameId;
	m_recentlyPlayedRow.setSuspended(isFeaturedGameSuspended);

	m_recentlyPlayedRow.update(deltaSeconds);
	m_emptyState.update(deltaSeconds);

	if (m_optionsMenuOpen) m_optionsMenu->update(deltaSeconds);

	Renderer::Frame frame;
	if (m_renderer.beginFrame(frame, contentClipTop)) {
		frame.draw(*m_background);
		m_navBar.draw(frame);
		m_widgetRow.draw(frame);
		m_collectionGrid.draw(frame);
		m_recentlyPlayedRow.draw(frame);
		m_emptyState.draw(frame);

		if (m_optionsMenuOpen) frame.draw(*m_optionsMenu);
		if (m_state == AppState::Launching) drawLaunch(frame);
		if (m_fadePhase != FadePhase::None) frame.draw(m_fadeOverlay);

		m_renderer.endFrame(frame);
	}

	updateFade(deltaSeconds);
}


void Dashboard::stepGame() {
	const Uint64 frameStartMs = SDL_GetTicks();
	const Uint64 nowNs = SDL_GetTicksNS();
	const float deltaSeconds = std::min(static_cast<float>(nowNs - m_lastFrameNs) / 1.0e9f, 0.05f);
	
	m_pendingPlaySeconds += std::min(static_cast<double>(nowNs - m_lastFrameNs) / 1.0e9, longestCountedFrameSeconds);
	if (m_pendingPlaySeconds >= playtimeAutosaveSeconds) commitPlaytime();

	m_lastFrameNs = nowNs;

	SDL_UpdateGamepads();

	m_game->runFrame();

	m_game->drainAudio(m_audioScratch);
	m_audio.queue(m_audioScratch);

	// The upload is its own copy pass, so it happens before the frame's render pass begins
	m_gameView.upload(m_game->getDisplayFrame());

	Renderer::Frame frame;
	if (m_renderer.beginFrame(frame, contentClipTop)) {
		frame.draw(m_gameView);
		if (m_fadePhase != FadePhase::None) frame.draw(m_fadeOverlay);
		m_renderer.endFrame(frame);
	}

	updateFade(deltaSeconds);

	// Frame pacing. The audio queue normally does the waiting, so this only matters when there's no sound.
	const Uint64 frameBudgetMs = Hardware::MILLISECONDS_PER_FRAME;
	const Uint64 elapsedMs = SDL_GetTicks() - frameStartMs;
	if (elapsedMs < frameBudgetMs) {
		SDL_Delay(static_cast<Uint32>(frameBudgetMs - elapsedMs));
	}
}


void Dashboard::buildUI() {
	m_rasterScale = m_renderer.computeRasterScale();

	m_background = std::make_unique<GameBackground>(m_renderer.device(), m_renderer.rectanglePipeline(), m_renderer.imagePipeline(), m_renderer.textPipeline(), m_renderer.sampler());
	m_background->setCreditFont(fontItalic, 20);
	m_background->setRasterScale(m_rasterScale);

	m_focus.setComponent(FocusManager::RowCollection, &m_collectionGrid);
	m_focus.setComponent(FocusManager::RowGames, &m_recentlyPlayedRow);

	m_focus.setOnChanged([this]() {
		// Landing on a tab selects it, no press needed
		if (const std::optional<Tab> tab = m_navBar.tabOf(m_focus.focusedTile())) setActiveTab(*tab);
	});

	rebuildLibrary();

	m_navBar.build(m_rasterScale);
	m_focus.addTile(FocusManager::RowNav, m_navBar.recentlyPlayedTab());
	m_focus.addTile(FocusManager::RowNav, m_navBar.collectionTab());
	m_focus.addTile(FocusManager::RowNav, m_navBar.searchIcon());
	m_focus.addTile(FocusManager::RowNav, m_navBar.trophiesIcon());

	// Moving up out of a tab's content lands on that tab, whichever tile was selected
	m_focus.setLeaveTarget(FocusManager::RowGames, m_navBar.recentlyPlayedTab());
	m_focus.setLeaveTarget(FocusManager::RowCollection, m_navBar.collectionTab());

	m_widgetRow.build(m_rasterScale, [this]() {
		std::vector<ControllerStatus> statuses;
		for (const ControllerInput::ControllerInfo& info : m_gamepad.getControllers()) {
			statuses.push_back({ info.playerNumber, info.batteryPercent, info.charging });
		}
		return statuses;
	});
	m_focus.addTile(FocusManager::RowWidgets, m_widgetRow.trophiesTile());

	applyTabVisibility();

	// Start on the games row, or on the nav row when there are no games to focus
	m_focus.focusRow(m_recentlyPlayedRow.empty() ? FocusManager::RowNav : FocusManager::RowGames, false);

	m_optionsMenu = std::make_unique<MenuTile>(m_renderer.rectanglePipeline(), m_renderer.textPipeline(), m_renderer.sampler());
	m_optionsMenu->setFont(fontRegular, 24);
	m_optionsMenu->setRasterScale(m_rasterScale);
}


void Dashboard::rebuildRasterTiles() {
	const float newScale = m_renderer.computeRasterScale();
	if (newScale <= 0.0f || std::abs(newScale - m_rasterScale) < 0.001f) return;
	m_rasterScale = newScale;

	m_navBar.setRasterScale(m_rasterScale);
	m_widgetRow.setRasterScale(m_rasterScale);
	m_collectionGrid.setRasterScale(m_rasterScale);
	m_recentlyPlayedRow.setRasterScale(m_rasterScale);
	m_emptyState.setRasterScale(m_rasterScale);
	m_optionsMenu->setRasterScale(m_rasterScale);
	m_background->setRasterScale(m_rasterScale);
	if (m_optionsMenuOpen) buildOptionsMenu();
}


void Dashboard::navigate(NavInput nav) {
	if (m_state == AppState::Launching || m_fadePhase != FadePhase::None) return; // no input while a transition runs

	// The options menu owns the controller while it is open
	if (m_optionsMenuOpen) {
		navigateOptionsMenu(nav);
		return;
	}

	if (nav == NavInput::Back) return;

	if (nav == NavInput::Options) {
		GameTile* tile = nullptr;
		if (m_focus.currentRow() == FocusManager::RowGames) tile = m_recentlyPlayedRow.selectedTile();
		else if (m_focus.currentRow() == FocusManager::RowCollection) tile = m_collectionGrid.selectedTile();

		if (tile) openOptionsMenu(tile);
		return;
	}

	// X on a game starts it (or, if it's the game that's idling, goes back to it). Everywhere else Select behaves as before.
	if (nav == NavInput::Select) {
		const GameInfo* game = nullptr;
		GameTile* tile = nullptr;
		if (m_focus.currentRow() == FocusManager::RowGames) {
			game = m_recentlyPlayedRow.selectedGame();
			tile = m_recentlyPlayedRow.selectedTile();
		}
		else if (m_focus.currentRow() == FocusManager::RowCollection) {
			game = m_collectionGrid.selectedGame();
			tile = m_collectionGrid.selectedTile();
		}

		// Disc 1 for now. Picking a disc for multi-disc games comes later.
		if (game && !game->discs.empty()) {
			const std::string& cuePath = game->discs.front().cuePath;

			if (m_state == AppState::Dashboard) {
				beginLaunch(cuePath, tile);
			}
			else if (cuePath == m_gameCuePath) {
				beginFade([this]() { resumeGame(); });
			}
			else {
				// One game at a time. Closing the idle game from the dashboard comes with the notification overlay.
				std::cout << "Echo: A game is already loaded. Press the PS button (or F1) to go back to it, Escape closes it." << std::endl;
			}
			return;
		}
	}

	if (nav == NavInput::Search) m_focus.focusTile(m_navBar.searchIcon());
	else m_focus.navigate(nav);
}


void Dashboard::setActiveTab(Tab tab) {
	if (!m_tabs.setActive(tab)) return;

	m_navBar.setActiveTab(tab);

	// Each row belongs to one tab
	const bool recent = (tab == Tab::RecentlyPlayed);
	m_focus.setRowEnabled(FocusManager::RowCollection, !recent);
	m_focus.setRowEnabled(FocusManager::RowGames, recent);
	m_focus.setRowEnabled(FocusManager::RowWidgets, recent);

	applyTabVisibility();
}


void Dashboard::applyTabVisibility() {
	const float recentOpacity = m_tabs.opacity(Tab::RecentlyPlayed);

	m_recentlyPlayedRow.setOpacity(recentOpacity);
	m_collectionGrid.setOpacity(m_tabs.opacity(Tab::Collection));

	// The widgets sit with the Recently Played tab
	m_widgetRow.setOpacity(recentOpacity);

	m_background->setOpacity(recentOpacity);
}


void Dashboard::handleDroppedPath(const char* dropped) {
	// Only while the "no games" screen is up
	if (!m_library.empty()) return;

	const std::string folder = GameLibrary::folderFromDroppedPath(dropped);
	if (folder.empty()) return;

	m_prefs.setString("gamesFolder", folder);
	m_prefs.save();

	m_library.scan(folder);
	rebuildLibrary();
}


void Dashboard::rebuildLibrary() {
	applyPlayHistory();
	m_recentlyPlayedRow.build(m_library, m_rasterScale);
	m_collectionGrid.build(m_library, m_rasterScale);
	m_emptyState.build(m_library, m_rasterScale);

	applyTabVisibility();
}


bool Dashboard::startGame(const std::string& cuePath) {
	if (m_game) return false; // one game at a time

	const char* basePath = SDL_GetBasePath();
	if (!basePath) return false;

	// The BIOS is copied next to the executable by CMake, so this works from any working directory
	const std::string biosPath = std::string(basePath) + "assets/SCPH1001.BIN";

	std::unique_ptr<PlayStation> game = std::make_unique<PlayStation>();
	if (!game->powerOn(biosPath, cuePath)) {
		std::cout << "Echo: Could not power on the console. Check the BIOS (" << biosPath << ") and the disc (" << cuePath << ")." << std::endl;
		return false;
	}

	// No sound is not fatal, the game just runs without it
	m_audio.open();

	// The controllers were plugged in before this console existed, so it never saw SDL announce them. Introduce each one now.
	int gamepadCount = 0;
	SDL_JoystickID* gamepadIds = SDL_GetGamepads(&gamepadCount);
	if (gamepadIds) {
		for (int i = 0; i < gamepadCount; i++) {
			SDL_Event added = {};
			added.type = SDL_EVENT_GAMEPAD_ADDED;
			added.gdevice.which = gamepadIds[i];
			game->handleEvent(added);
		}
		SDL_free(gamepadIds);
	}

	m_game = std::move(game);
	m_gameCuePath = cuePath;
	m_state = AppState::GameRunning;
	m_renderer.setClearColor(gameClearColor);

	std::cout << "Echo: Launching " << cuePath << std::endl;
	return true;
}


void Dashboard::toggleGame() {
	if (m_fadePhase != FadePhase::None) return; // already switching

	if (m_state == AppState::GameRunning) beginFade([this]() { pauseGame(); });
	else if (m_state == AppState::GameIdle) beginFade([this]() { closeOptionsMenu(); resumeGame(); });
}


void Dashboard::pauseGame() {
	if (m_state != AppState::GameRunning) return;

	commitPlaytime();

	// runFrame() simply stops being called, and the audio stops with it
	m_audio.pause();
	m_state = AppState::GameIdle;
	m_renderer.setClearColor(Renderer::defaultClearColor);

	// Coming back from a game always lands on that game: the first tile of Recently Played
	setActiveTab(Tab::RecentlyPlayed);
	m_recentlyPlayedRow.jumpToFirst();
	m_focus.focusRow(FocusManager::RowGames, false);
}


void Dashboard::resumeGame() {
	if (m_state != AppState::GameIdle) return;

	m_audio.resume();
	m_state = AppState::GameRunning;
	m_renderer.setClearColor(gameClearColor);
}


void Dashboard::stopGame() {
	commitPlaytime();
	m_audio.close();
	m_game.reset();
	m_gameCuePath.clear();

	m_state = AppState::Dashboard;
	m_renderer.setClearColor(Renderer::defaultClearColor);

	std::cout << "Echo: Game closed" << std::endl;

	m_playingGameId.clear();
	m_pendingPlaySeconds = 0.0;
}


void Dashboard::beginLaunch(const std::string& cuePath, GameTile* tile) {
	m_launchTile = tile;
	m_launchCuePath = cuePath;
	m_launchSeconds = 0.0f;
	m_launchStartRect = SDL_FRect{ tile->getX(), tile->getY(), tile->getWidth(), tile->getHeight() };
	m_launchWasClipped = tile->isClippedToContent();
	m_launchStartCornerRadius = tile->getCornerRadius();
	m_launchWasFocused = tile->isFocused();
	tile->setFocused(false); // no focus border while it grows
	m_launchDim.setDrawOpacity(0.0f);

	tile->setDriven(true);
	tile->setClippedToContent(false); // the grid cuts tiles off above the navigation, and a growing tile must not be cut
	m_state = AppState::Launching;
}


void Dashboard::updateLaunch(float deltaSeconds) {
	if (m_state != AppState::Launching || !m_launchTile) return;

	m_launchSeconds = std::min(m_launchSeconds + deltaSeconds, launchGrowSeconds);
	const float progress = easeInOutCubic(m_launchSeconds / launchGrowSeconds);

	const float endX = (Renderer::designWidth - launchEndSize) * 0.5f;
	const float endY = (Renderer::designHeight - launchEndSize) * 0.5f;
	m_launchTile->placeAt(interpolate(m_launchStartRect.x, endX, progress),
	                      interpolate(m_launchStartRect.y, endY, progress),
	                      interpolate(m_launchStartRect.w, launchEndSize, progress),
	                      interpolate(m_launchStartRect.h, launchEndSize, progress));
	
	m_launchTile->setCornerRadius(interpolate(m_launchStartCornerRadius, 0.0f, progress));

	// Everything else on the dashboard melts into the background while the tile grows
	m_launchDim.setDrawOpacity(progress);

	// Once it has grown, the tile sits there while the black fade runs over it
	if (m_fadePhase == FadePhase::None && m_launchSeconds >= launchGrowSeconds * launchFadeStartFraction) {
		beginFade([this]() { finishLaunch(); });
	}
}


void Dashboard::drawLaunch(Renderer::Frame& frame) {
	frame.draw(m_launchDim);
	if (m_launchTile) frame.draw(*m_launchTile); // on top of the dim layer, so it isn't dimmed itself
}


void Dashboard::finishLaunch() {
	const std::string gameId = m_launchTile->getInfo().id;
	// The screen is black, so the tile can go back where it belongs and the console can take its time starting
	m_launchTile->setDriven(false);
	m_launchTile->snapToTarget();
	m_launchTile->setCornerRadius(m_launchStartCornerRadius);
	m_launchTile->setFocused(m_launchWasFocused);
	m_launchTile->setClippedToContent(m_launchWasClipped);
	m_launchTile = nullptr;
	m_launchDim.setDrawOpacity(0.0f);

	if (startGame(m_launchCuePath)) recordPlayed(gameId);
	else m_state = AppState::Dashboard;
}


void Dashboard::beginFade(std::function<void()> atBlack) {
	m_fadeAtBlack = std::move(atBlack);
	m_fadePhase = FadePhase::Out;
	m_fadeSeconds = 0.0f;
	m_fadeOverlay.setDrawOpacity(0.0f);
}


void Dashboard::updateFade(float deltaSeconds) {
	if (m_fadePhase == FadePhase::None) return;

	m_fadeSeconds += deltaSeconds;

	if (m_fadePhase == FadePhase::Out) {
		const float progress = std::min(m_fadeSeconds / fadeOutSeconds, 1.0f);
		m_fadeOverlay.setDrawOpacity(progress);

		if (progress >= 1.0f) {
			if (m_fadeAtBlack) m_fadeAtBlack();
			m_fadeAtBlack = nullptr;
			m_fadePhase = FadePhase::In;
			m_fadeSeconds = 0.0f;
		}
	}
	else {
		const float progress = std::min(m_fadeSeconds / fadeInSeconds, 1.0f);
		m_fadeOverlay.setDrawOpacity(1.0f - progress);
		if (progress >= 1.0f) m_fadePhase = FadePhase::None;
	}
}


// Copies the saved timestamps onto the games. Has to run after every scan, because a scan starts from fresh GameInfo.
void Dashboard::applyPlayHistory() {
	for (const std::string& id : m_library.allGames()) {
		if (const std::optional<int64_t> lastPlayed = m_userData.lastPlayed(id)) m_library.setLastPlayed(id, *lastPlayed);
		m_library.setTimePlayedSeconds(id, m_userData.timePlayedSeconds(id));
	}
}


void Dashboard::recordPlayed(const std::string& gameId) {
	m_playingGameId = gameId;
	m_pendingPlaySeconds = 0.0;

	SDL_Time now = 0;
	SDL_GetCurrentTime(&now); // nanoseconds since 1970
	const int64_t timestamp = now / SDL_NS_PER_SECOND;

	m_userData.setLastPlayed(gameId, timestamp);
	m_userData.save(); // right away, so a crash during the game doesn't lose it

	// Only Recently Played is rebuilt. Rebuilding the Collection grid would throw away its scroll position.
	m_library.setLastPlayed(gameId, timestamp);
	m_recentlyPlayedRow.build(m_library, m_rasterScale);
}

// Adds the time played since the last commit to the saved total and tells everything that shows it
void Dashboard::commitPlaytime() {
	const int64_t wholeSeconds = static_cast<int64_t>(m_pendingPlaySeconds);
	if (m_playingGameId.empty() || wholeSeconds <= 0) return;

	m_pendingPlaySeconds -= static_cast<double>(wholeSeconds); // the fraction carries over to the next commit

	const int64_t total = m_userData.addTimePlayedSeconds(m_playingGameId, wholeSeconds);
	m_userData.save();

	m_library.setTimePlayedSeconds(m_playingGameId, total);
	m_recentlyPlayedRow.setTimePlayedSeconds(m_playingGameId, total);
	m_collectionGrid.setTimePlayedSeconds(m_playingGameId, total);
}


std::vector<MenuRowInfo> Dashboard::optionsMenuRows(const GameInfo& info) const {
	std::vector<MenuRowInfo> rows;

	const bool isMultiDisc = info.discs.size() > 1;
	const bool isLoadedGame = m_game && m_playingGameId == info.id;

	// Which disc of this game is in the console, or -1 when this game isn't the loaded one
	int loadedDiscIndex = -1;
	if (isLoadedGame) {
		for (int index = 0; index < static_cast<int>(info.discs.size()); ++index) {
			if (info.discs[index].cuePath == m_gameCuePath) loadedDiscIndex = index;
		}
	}

	// Close is always first, and only on the game that is loaded
	if (isLoadedGame) {
		const std::string closeLabel = (isMultiDisc && loadedDiscIndex >= 0) ? "Close Disc " + std::to_string(loadedDiscIndex + 1) : "Close Game";
		rows.push_back({ MenuAction::CloseGame, closeLabel, "icons/close.svg", SDL_FColor{ 0.75f, 0.30f, 0.30f, 1.0f } });
	}

	// Play is offered for every disc except the one that is in the console. On any other game, loadedDiscIndex is -1, so every disc shows.
	for (int index = 0; index < static_cast<int>(info.discs.size()); ++index) {
		if (index == loadedDiscIndex) continue;

		const std::string playLabel = isMultiDisc ? "Play Disc " + std::to_string(index + 1) : "Play Game";
		rows.push_back({ MenuAction::PlayDisc, playLabel, "icons/disc.svg", SDL_FColor{ 1.0f, 1.0f, 1.0f, 1.0f }, index });
	}

	// Your existing rows, unchanged
	rows.push_back({ MenuAction::ManageSaves,     "Manage Saves",       "icons/memorycard.svg" });
	rows.push_back({ MenuAction::ViewTrophies,    "View Echo Trophies", "icons/trophy.svg" });
	rows.push_back({ MenuAction::GameInformation, "Game Information",   "icons/info.svg" });
	rows.push_back({ MenuAction::ToggleFavorite,  "Add to Favorites",   "icons/bookmark.svg" });

	return rows;
}


void Dashboard::openOptionsMenu(GameTile* tile) {
	m_optionsGameId = tile->getInfo().id;
	m_optionsTile = tile;

	m_optionsMenu->setRows(optionsMenuRows(tile->getInfo()));
	buildOptionsMenu();
	m_optionsMenu->openBeside(*tile);

	m_optionsMenuOpen = true;
}


void Dashboard::closeOptionsMenu() {
	if (!m_optionsMenuOpen) return;
	m_optionsTile = nullptr;
	m_optionsMenuOpen = false;

	// No rows means the icon and text textures are released while the menu is closed
	m_optionsMenu->setRows({});
}


void Dashboard::buildOptionsMenu() {
	SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(m_renderer.device());
	if (!uploadCmd) return;

	m_optionsMenu->build(m_renderer.device(), uploadCmd);
	SDL_SubmitGPUCommandBuffer(uploadCmd);
}


void Dashboard::navigateOptionsMenu(NavInput nav) {
	switch (nav) {
	case NavInput::Up:
		m_optionsMenu->moveSelection(-1);
		break;

	case NavInput::Down:
		m_optionsMenu->moveSelection(1);
		break;

	case NavInput::Back:
		closeOptionsMenu();
		break;

	case NavInput::Select: {
		const MenuRowInfo* row = m_optionsMenu->selectedRow();
		if (!row) break;

		// Closing clears the rows and the tile, so take what is needed before that
		const MenuAction action = row->action;
		const int argument = row->argument;
		GameTile* tile = m_optionsTile;
		closeOptionsMenu();

		runOptionsAction(action, argument, tile);
		break;
	}

	default:
		break; // everything else is locked out while the menu is open
	}
}

void Dashboard::runOptionsAction(MenuAction action, int argument, GameTile* tile) {
	switch (action) {
	case MenuAction::CloseGame:
		// The same thing Escape does while a game is running
		if (m_game) stopGame();
		break;

	case MenuAction::PlayDisc: {
		if (!tile) break;

		const GameInfo& info = tile->getInfo();
		if (argument < 0 || argument >= static_cast<int>(info.discs.size())) break;
		const std::string cuePath = info.discs[argument].cuePath;

		// Switching discs is a close followed by a launch. Nothing else is rebuilt in between, so the tile is still valid.
		if (m_game) stopGame();
		beginLaunch(cuePath, tile);
		break;
	}

	default:
		break; // the other rows come later
	}
}
