#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

using namespace geode::prelude;

class $modify(LevelHashLayer, LevelInfoLayer) {
	bool init(GJGameLevel* level, bool challenge) {
		if (!LevelInfoLayer::init(level, challenge))
			return false;

		auto menu = getChildByID("left-side-menu");
		if (!menu) {
			log::warn("Could not find left-side-menu!");
			return true;
		}

		auto sprite = CircleButtonSprite::createWithSpriteFrameName(
			"GJ_levelHashIcon.png"_spr,
			0.8f,
			CircleBaseColor::Green,
			CircleBaseSize::Medium
		);

		if (!sprite) {
			log::warn("Failed to load level hash icon.");
			return true;
		}

		auto button = CCMenuItemExt::createSpriteExtra(
			sprite,
			[this](CCMenuItemSpriteExtra*) {
				showLevelFingerprint();
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
			showMessage("No level is available.");
			return;
		}

		auto const& data = m_level->m_levelString;

		if (data.empty()) {
			showMessage("Level data is empty or has not loaded yet.");
			return;
		}

		geode::Sha256Hasher hasher;
		hasher.update(data.data(), data.size());

		auto fingerprint = hasher.finish().toString();

		log::info("Level Hash: {}", fingerprint);
		showMessage(fingerprint);
	}

private:
	void showMessage(std::string const& message) {
		FLAlertLayer::create(
			"Level Hash",
			message,
			"OK"
		)->show();
	}
};
