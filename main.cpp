#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

// Simple SHA-256 hashing system for fingerprinting Geometry Dash levels.
// This can be used to determine whether level edit or something similar was used to make nerfs to a level secretly.
// Made by urcool626

using namespace geode::prelude;

class $modify(LevelHashLayer, LevelInfoLayer) {
	bool init(GJGameLevel* level, bool challenge) {
		if (!LevelInfoLayer::init(level, challenge))
			return false;

		auto menu = this->getChildByID("left-side-menu");

		if (!menu) {
			// Fallback
			log::warn("Could not find left-side-menu!");
			return true;
		}

		// Create the mod's button
		auto sprite = CircleButtonSprite::createWithSpriteFrameName(
			"GJ_levelHashIcon.png"_spr,
			0.8f,
			CircleBaseColor::Green,
			CircleBaseSize::Medium
		);

		if (!sprite) {
			// Fallback
			log::warn("Failed to load GJ_levelHashIcon.png");
			return true;
		}

		auto button = CCMenuItemExt::createSpriteExtra(
			sprite,
			[this](CCMenuItemSpriteExtra*) {
				this->showLevelFingerprint();
			}
		);

		if (!button)
			return true;

		button->setID("level-fingerprint-button");
		button->setPosition({0.f, -100.f});

		menu->addChild(button);

		return true;
	}

	void showLevelFingerprint() {
		if (!m_level) {
			FLAlertLayer::create(
				"Level Hash",
				"No level is available.",
				"OK"
			)->show();
			return;
		}

		// Hash the level data
		auto const& levelData = m_level->m_levelString;

		if (levelData.empty()) {
			// Fallback
			FLAlertLayer::create(
				"Level Hash",
				"The level data is empty or has not loaded yet.",
				"OK"
			)->show();
			return;
		}

		geode::Sha256Hasher hasher;
		hasher.update(levelData.data(), levelData.size());

		auto fingerprint = hasher.finish().toString();

		// Return hashed level data
		FLAlertLayer::create(
			"Level Hash",
			fingerprint.c_str(),
							 "OK"
		)->show();

		log::info("Level Hash: {}", fingerprint);
	}
};
