#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QTimer>
#include <cstring>

OBS_DECLARE_MODULE()

namespace {
QString result_dir;
QString phase;
QJsonObject evidence;
constexpr const char *source_name = "Pitch install source";

void finish(bool passed, const QString &reason)
{
	QJsonObject result{{"passed", passed},
			   {"reason", reason},
			   {"obs", obs_get_version_string()},
			   {"phase", phase},
			   {"evidence", evidence}};
	if (auto *module = obs_get_module("simple-pitchshift"))
		result.insert("product_binary_path", QString::fromUtf8(obs_get_module_binary_path(module)));
	QFile file(result_dir + "/result.json");
	const auto json = QJsonDocument(result).toJson();
	if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.flush())
		passed = false;
	file.close();
	blog(passed ? LOG_INFO : LOG_ERROR, "PITCH_INSTALL %s phase=%s %s", passed ? "PASS" : "FAIL",
	     phase.toUtf8().constData(), reason.toUtf8().constData());
	auto *window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	QTimer::singleShot(0, window, [window] { window->close(); });
}

void inspect()
{
	auto *module = obs_get_module("simple-pitchshift");
	if (!module || QString(obs_get_version_string()) != "33.0.1") {
		finish(false, "product module missing or OBS version mismatch");
		return;
	}
	int registrations = 0;
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_filter_types(i, &id); ++i)
		registrations += std::strcmp(id, "simple_pitchshift_filter") == 0;
	evidence.insert("filter_registrations", registrations);
	if (registrations != 1) {
		finish(false, "filter registration count mismatch");
		return;
	}
	char *locale = obs_find_module_file(module, "locale/en-US.ini");
	char *japanese_locale = obs_find_module_file(module, "locale/ja-JP.ini");
	const QString translated = QString::fromUtf8(obs_module_get_locale_text(module, "SimplePitchShiftFilterName"));
	evidence.insert("locale_path", locale ? QString::fromUtf8(locale) : QString());
	evidence.insert("japanese_locale_path", japanese_locale ? QString::fromUtf8(japanese_locale) : QString());
	evidence.insert("filter_name", translated);
	const QString expected_name = phase == "legacy-setup" ? "Simple" : "Simple Pitch Shift";
	const bool resources = locale && japanese_locale && translated == expected_name;
	bfree(locale);
	bfree(japanese_locale);
	if (!resources) {
		finish(false, "installed translated locale unavailable");
		return;
	}
	obs_source_t *source = nullptr;
	obs_source_t *filter = nullptr;
	if (phase == "setup" || phase == "legacy-setup") {
		source = obs_source_create("pitch_install_source", source_name, nullptr, nullptr);
		auto *settings = obs_data_create();
		obs_data_set_int(settings, "semitone", 3);
		filter = obs_source_create("simple_pitchshift_filter", "install pitch", settings, nullptr);
		obs_data_release(settings);
		auto *scene_source = obs_frontend_get_current_scene();
		auto *scene = scene_source ? obs_scene_from_source(scene_source) : nullptr;
		const bool attached = source && filter && scene && obs_scene_add(scene, source);
		obs_source_release(scene_source);
		if (!attached) {
			obs_source_release(filter);
			obs_source_release(source);
			finish(false, "source, filter or scene attachment failed");
			return;
		}
		obs_source_filter_add(source, filter);
	} else {
		source = obs_get_source_by_name(source_name);
		if (source)
			filter = obs_source_get_filter_by_name(source, "install pitch");
	}
	if (!source || !filter || std::strcmp(obs_source_get_id(source), "pitch_install_source") != 0 ||
	    std::strcmp(obs_source_get_id(filter), "simple_pitchshift_filter") != 0) {
		obs_source_release(filter);
		obs_source_release(source);
		finish(false, "persisted source or filter missing");
		return;
	}
	auto *settings = obs_source_get_settings(filter);
	const bool retained = obs_source_enabled(filter) && obs_data_get_int(settings, "semitone") == 3;
	evidence.insert("filter_settings", QJsonDocument::fromJson(obs_data_get_json(settings)).object());
	evidence.insert("filter_enabled", obs_source_enabled(filter));
	obs_data_release(settings);
	obs_source_release(filter);
	obs_source_release(source);
	if (retained)
		obs_frontend_save();
	finish(retained, "installed locale and persisted filter settings checked");
}

void frontend_event(obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
		QTimer::singleShot(1000, static_cast<QMainWindow *>(obs_frontend_get_main_window()), inspect);
}
} // namespace

bool obs_module_load()
{
	result_dir = qEnvironmentVariable("PITCH_INSTALL_RESULT_DIR");
	phase = qEnvironmentVariable("PITCH_INSTALL_PHASE");
	if (result_dir.isEmpty() || (phase != "setup" && phase != "legacy-setup" && phase != "restore"))
		return false;
	// 配置と設定保持だけを観測し、音声デバイスやDSP検証を起動しない。
	obs_source_info info{};
	info.id = "pitch_install_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_AUDIO;
	info.get_name = [](void *) {
		return "Pitch install source";
	};
	info.create = [](obs_data_t *, obs_source_t *source) -> void * {
		return source;
	};
	info.destroy = [](void *) {
	};
	obs_register_source(&info);
	obs_frontend_add_event_callback(frontend_event, nullptr);
	return true;
}

void obs_module_unload()
{
	obs_frontend_remove_event_callback(frontend_event, nullptr);
}
