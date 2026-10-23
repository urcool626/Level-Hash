#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

using namespace geode::prelude;

namespace {
	constexpr auto HASHES_URL = "https://urcool626.github.io/Level-Hash/hashes.json";

	matjson::Value s_hashes;
	bool s_loadedCache = false;
	bool s_fetchInProgress = false;
	std::chrono::steady_clock::time_point s_lastFetch{};

	// Convert a hash to lowercase for comparison.
	std::string normalizeHash(std::string hash) {
		std::transform(
			hash.begin(),
					   hash.end(),
					   hash.begin(),
					   [](unsigned char c) {
						   return static_cast<char>(std::tolower(c));
					   }
		);

		return hash;
	}

	// SHA-256 hashes contain exactly 64 hexadecimal characters.
	bool isValidHash(std::string const& hash) {
		return hash.size() == 64 &&
		std::all_of(
			hash.begin(),
					hash.end(),
					[](unsigned char c) {
						return std::isxdigit(c) != 0;
					}
		);
	}

	// Validate the downloaded or cached JSON database.
	bool isValidDatabase(matjson::Value const& json) {
		if (!json.isObject())
			return false;

		for (auto const& [id, value] : json) {
			auto hash = value.asString();

			if (!hash || !isValidHash(hash.unwrap()))
				return false;
		}

		return true;
	}

	// Load the most recently saved database.
	void loadCachedDatabase() {
		if (s_loadedCache)
			return;

		auto cachePath = Mod::get()->getSaveDir() / "hashes.json";
		std::ifstream file(cachePath, std::ios::binary);

		if (!file) {
			log::info("No cached hash database found.");
			s_hashes = matjson::Value();
			s_loadedCache = true;
			return;
		}

		std::string contents{
			std::istreambuf_iterator<char>{file},
			std::istreambuf_iterator<char>{}
		};

		auto parsed = matjson::parse(contents);

		if (!parsed || !isValidDatabase(parsed.unwrap())) {
			log::warn("Cached hash database is invalid.");
			s_hashes = matjson::Value();
			s_loadedCache = true;
			return;
		}

		s_hashes = parsed.unwrap();
		s_loadedCache = true;

		log::info(
			"Loaded cached hash database ({} entries).",
				  s_hashes.size()
		);
	}


	// Save a database using a temporary file so an incomplete download
	// does not overwrite the existing database.
	bool saveDatabase(std::string const& contents) {
		auto directory = Mod::get()->getSaveDir();
		auto path = directory / "hashes.json";
		auto tempPath = directory / "hashes.json.tmp";
		auto backupPath = directory / "hashes.json.bak";

		{
			std::ofstream file(
				tempPath,
				std::ios::binary | std::ios::trunc
			);

			if (!file)
				return false;

			file.write(
				contents.data(),
					   static_cast<std::streamsize>(contents.size())
			);

			file.flush();

			if (!file)
				return false;
		}

		std::error_code ec;
		bool hadOldFile =
		std::filesystem::exists(path, ec) && !ec;

		if (hadOldFile) {
			std::filesystem::remove(backupPath, ec);
			ec.clear();

			std::filesystem::rename(path, backupPath, ec);

			if (ec) {
				std::filesystem::remove(tempPath, ec);
				return false;
			}
		}

		ec.clear();
		std::filesystem::rename(tempPath, path, ec);

		if (ec) {
			if (hadOldFile) {
				std::error_code restoreError;

				std::filesystem::rename(
					backupPath,
					path,
					restoreError
				);
			}

			std::error_code cleanupError;
			std::filesystem::remove(tempPath, cleanupError);

			return false;
		}

		if (hadOldFile)
			std::filesystem::remove(backupPath, ec);

		return true;
	}

	// Download the database at most once every five minutes.
	void updateDatabase() {
		auto now = std::chrono::steady_clock::now();

		if (
			s_fetchInProgress ||
			(
				s_lastFetch.time_since_epoch().count() != 0 &&
				now - s_lastFetch < std::chrono::minutes(5)
			)
		) {
			return;
		}

		s_fetchInProgress = true;
		s_lastFetch = now;

		auto request = web::WebRequest();
		request.timeout(std::chrono::seconds(10));

		async::spawn(
			request.get(HASHES_URL),
					 [](web::WebResponse response) {
						 s_fetchInProgress = false;

						 if (!response.ok()) {
							 log::warn(
								 "Hash database request failed: HTTP {}",
				  response.code()
							 );
							 return;
						 }

						 auto contents = response.string().unwrapOr("");
						 auto parsed = matjson::parse(contents);

						 if (!parsed || !isValidDatabase(parsed.unwrap())) {
							 log::warn("Downloaded hash database is invalid.");
							 return;
						 }

						 auto newDatabase = parsed.unwrap();

						 if (s_hashes == newDatabase) {
							 log::debug("Hash database is already up to date.");
							 return;
						 }

						 if (!saveDatabase(contents)) {
							 log::warn("Failed to save hash database.");
							 return;
						 }

						 s_hashes = newDatabase;

						 log::info(
							 "Hash database updated ({} entries).",
								   s_hashes.size()
						 );
					 }
		);
	}

	// Calculate the SHA-256 fingerprint of the level data.
	std::string calculateHash(std::string const& data) {
		geode::Sha256Hasher hasher;
		hasher.update(data.data(), data.size());

		return hasher.finish().toString();
	}
}

class $modify(LevelHashLayer, LevelInfoLayer) {
	bool init(GJGameLevel* level, bool challenge) {
		if (!LevelInfoLayer::init(level, challenge))
			return false;

		loadCachedDatabase();
		updateDatabase();

		auto menu = getChildByID("left-side-menu");

		if (!menu) {
			log::warn("Could not find left-side-menu!");
			return true;
		}

		// Green button: calculate the hash and show the comparison.
		auto sprite = CircleButtonSprite::createWithSpriteFrameName(
			"GJ_levelHashIcon.png"_spr,
			0.8f,
			CircleBaseColor::Green,
			CircleBaseSize::Medium
		);

		if (sprite) {
			auto button = CCMenuItemExt::createSpriteExtra(
				sprite,
				[this](CCMenuItemSpriteExtra*) {
					showLevelFingerprint();
				}
			);

			if (button) {
				button->setID("level-fingerprint-button");
				button->setPosition({0.f, -100.f});
				menu->addChild(button);
			}
		}
		else {
			log::warn("Failed to load level hash icon.");
		}

		// Copy button: copy the calculated hash to the clipboard.
		auto copyLabel = CCLabelBMFont::create(
			"Copy",
			"bigFont.fnt"
		);

		if (copyLabel) {
			copyLabel->setScale(0.45f);

			auto copyButton = CCMenuItemExt::createSpriteExtra(
				copyLabel,
				[this](CCMenuItemSpriteExtra* sender) {
					copyLevelFingerprint(sender);
				}
			);

			if (copyButton) {
				copyButton->setID("level-fingerprint-copy-button");
				copyButton->setPosition({35.f, -100.f});
				menu->addChild(copyButton);
			}
		}

		return true;
	}

	// Display the hash and its status against the reference database.
	void showLevelFingerprint() {
		if (!m_level) {
			showMessage("No level is available.");
			return;
		}

		auto const& data = m_level->m_levelString;

		if (data.empty()) {
			showMessage(
				"Level data is empty or has not loaded yet."
			);
			return;
		}

		auto fingerprint = calculateHash(data);
		auto levelID = std::to_string(m_level->m_levelID);

		log::info("Level Hash: {}", fingerprint);
		log::info("Looking up level ID: '{}'", levelID);
		log::info("Database is object: {}", s_hashes.isObject());
		log::info("Database entries: {}", s_hashes.size());

		if (s_hashes.isObject()) {
			log::info(
				"Database contains level ID '{}': {}",
			 levelID,
			 s_hashes.contains(levelID)
			);
		}

		if (s_hashes.isObject()) {
			for (auto const& [id, value] : s_hashes) {
				log::info("Loaded database key: '{}'", id);
			}
		}

		if (!s_hashes.isObject() || !s_hashes.contains(levelID)) {
			showMessage(
				"<cg>Level Hash:</c>\n<cj>" + fingerprint + "</c>" +
				"\n\n<c_>Status: Unknown level (not in database).</c>"
			);
			return;
		}

		auto expectedResult = s_hashes[levelID].asString();

		if (!expectedResult) {
			showMessage("<c_>The reference hash is invalid.</c>");
			return;
		}

		auto expected = normalizeHash(expectedResult.unwrap());
		auto actual = normalizeHash(fingerprint);

		if (!isValidHash(expected)) {
			showMessage("<c_>The reference hash is invalid.</c>");
			return;
		}

		if (actual == expected) {
			showMessage(
				std::string(
					"<cg>Hash matches: </c><cj>"
				) + levelID + "</c><cy>\n" + fingerprint + "</c>"
			);
		}
		else {
			showMessage(
				"HASH MISMATCH!\n\n"
				"Current: " + fingerprint +
				"\n\nExpected: " + expected +
				"\n\nThe level data differs from the reference."
			);
		}
	}

	// Copy the fingerprint to the system clipboard.
	void copyLevelFingerprint(CCObject* sender) {
		if (!sender || !m_level)
			return;

		auto const& data = m_level->m_levelString;

		if (data.empty()) {
			showMessage(
				"<c_>Level data is empty or has not loaded yet.</c>"
			);
			return;
		}

		auto fingerprint = calculateHash(data);

		geode::utils::clipboard::write(fingerprint);

		Notification::create(
			"SHA-256 hash copied to clipboard!",
			NotificationIcon::None,
			2.f
		)->show();
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
