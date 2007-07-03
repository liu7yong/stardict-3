#include "pluginmanager.h"
#include "file.hpp"
#include <string>

StarDictPluginBaseObject::StarDictPluginBaseObject(const char *filename, GModule *module_, plugin_configure_func_t configure_func_):
	plugin_filename(filename), module(module_), configure_func(configure_func_)
{
}

StarDictPluginBase::StarDictPluginBase(StarDictPluginBaseObject *baseobj_):
	baseobj(baseobj_)
{
}

typedef void (*stardict_plugin_exit_func_t)(void);

StarDictPluginBase::~StarDictPluginBase()
{
	union {
		stardict_plugin_exit_func_t stardict_plugin_exit;
		gpointer stardict_plugin_exit_avoid_warning;
	} func;
	func.stardict_plugin_exit = 0;
	if (g_module_symbol (baseobj->module, "stardict_plugin_exit", (gpointer *)&(func.stardict_plugin_exit_avoid_warning))) {
		func.stardict_plugin_exit();
	}
	g_module_close (baseobj->module);
}

void StarDictPluginBase::configure()
{
	if (baseobj->configure_func != NULL)
		baseobj->configure_func();
}

const char *StarDictPluginBase::get_filename()
{
	return baseobj->plugin_filename.c_str();
}

StarDictPlugins::StarDictPlugins(const char *dirpath, const std::list<std::string>& order_list, const std::list<std::string>& disable_list)
{
	plugindirpath = dirpath;
	load(dirpath, order_list, disable_list);
}

StarDictPlugins::~StarDictPlugins()
{
}

class PluginLoader {
public:
	PluginLoader(StarDictPlugins& plugins_): plugins(plugins_) {}
	void operator()(const std::string& url, bool disable) {
		if (!disable)
			plugins.load_plugin(url.c_str());
	}
private:
	StarDictPlugins& plugins;
};

void StarDictPlugins::load(const char *dirpath, const std::list<std::string>& order_list, const std::list<std::string>& disable_list)
{
	std::list<std::string> plugins_dirs;
	plugins_dirs.push_back(dirpath);
	for_each_file(plugins_dirs, "."G_MODULE_SUFFIX, order_list, disable_list, PluginLoader(*this));
}

void StarDictPlugins::reorder(const std::list<std::string>& order_list)
{
	VirtualDictPlugins.reorder(order_list);
	TtsPlugins.reorder(order_list);
	MiscPlugins.reorder(order_list);
}

bool StarDictPlugins::get_loaded(const char *filename)
{
	bool found = false;
	for (std::list<std::string>::iterator iter = loaded_plugin_list.begin(); iter != loaded_plugin_list.end(); ++iter) {
		if (*iter == filename) {
			found = true;
			break;
		}
	}
	return found;
}

class PluginInfoLoader {
public:
	PluginInfoLoader(StarDictPlugins& plugins_, std::list<StarDictPluginInfo> &virtualdict_pluginlist_, std::list<StarDictPluginInfo> &tts_pluginlist_, std::list<StarDictPluginInfo> &misc_pluginlist_): plugins(plugins_), virtualdict_pluginlist(virtualdict_pluginlist_), tts_pluginlist(tts_pluginlist_), misc_pluginlist(misc_pluginlist_) {}
	void operator()(const std::string& url, bool disable) {
		if (!disable) {
			StarDictPlugInType plugin_type = StarDictPlugInType_UNKNOWN;
			std::string info_xml;
			bool can_configure;
			plugins.get_plugin_info(url.c_str(), plugin_type, info_xml, can_configure);
			if (plugin_type != StarDictPlugInType_UNKNOWN && (!info_xml.empty())) {
				StarDictPluginInfo plugin_info;
				plugin_info.filename = url;
				plugin_info.plugin_type = plugin_type;
				plugin_info.info_xml = info_xml;
				plugin_info.can_configure = can_configure;
				switch (plugin_type) {
					case StarDictPlugInType_VIRTUALDICT:
						virtualdict_pluginlist.push_back(plugin_info);
						break;
					case StarDictPlugInType_TTS:
						tts_pluginlist.push_back(plugin_info);
						break;
					case StarDictPlugInType_MISC:
						misc_pluginlist.push_back(plugin_info);
						break;
					default:
						break;
				}
			}
		}
	}
private:
	StarDictPlugins& plugins;
	std::list<StarDictPluginInfo> &virtualdict_pluginlist;
	std::list<StarDictPluginInfo> &tts_pluginlist;
	std::list<StarDictPluginInfo> &misc_pluginlist;
};

void StarDictPlugins::get_plugin_list(const std::list<std::string>& order_list, std::list<std::pair<StarDictPlugInType, std::list<StarDictPluginInfo> > > &plugin_list)
{
	plugin_list.clear();
	std::list<StarDictPluginInfo> virtualdict_pluginlist;
	std::list<StarDictPluginInfo> tts_pluginlist;
	std::list<StarDictPluginInfo> misc_pluginlist;

	std::list<std::string> plugins_dirs;
	plugins_dirs.push_back(plugindirpath);
	std::list<std::string> disable_list;
	for_each_file(plugins_dirs, "."G_MODULE_SUFFIX, order_list, disable_list, PluginInfoLoader(*this, virtualdict_pluginlist, tts_pluginlist, misc_pluginlist));

	if (!virtualdict_pluginlist.empty()) {
		plugin_list.push_back(std::pair<StarDictPlugInType, std::list<StarDictPluginInfo> >(StarDictPlugInType_VIRTUALDICT, virtualdict_pluginlist));
	}
	if (!tts_pluginlist.empty()) {
		plugin_list.push_back(std::pair<StarDictPlugInType, std::list<StarDictPluginInfo> >(StarDictPlugInType_TTS, tts_pluginlist));
	}
	if (!misc_pluginlist.empty()) {
		plugin_list.push_back(std::pair<StarDictPlugInType, std::list<StarDictPluginInfo> >(StarDictPlugInType_MISC, misc_pluginlist));
	}
}

typedef bool (*stardict_plugin_init_func_t)(StarDictPlugInObject *obj);

void StarDictPlugins::get_plugin_info(const char *filename, StarDictPlugInType &plugin_type, std::string &info_xml, bool &can_configure)
{
	GModule *module;
	module = g_module_open (filename, G_MODULE_BIND_LAZY);
	if (!module) {
		g_print("Load %s failed!\n", filename);
		return;
	}
	union {
		stardict_plugin_init_func_t stardict_plugin_init;
		gpointer stardict_plugin_init_avoid_warning;
	} func;
	func.stardict_plugin_init = 0;
	if (!g_module_symbol (module, "stardict_plugin_init", (gpointer *)&(func.stardict_plugin_init_avoid_warning))) {
		g_print("Load %s failed: No stardict_plugin_init func!\n", filename);
		g_module_close (module);
		return;
	}
	StarDictPlugInObject *plugin_obj = new StarDictPlugInObject();
	bool failed = func.stardict_plugin_init(plugin_obj);
	if (failed) {
		g_print("Load %s failed!\n", filename);
		g_module_close (module);
		delete plugin_obj;
		return;
	}
	plugin_type = plugin_obj->type;
	info_xml = plugin_obj->info_xml;
	can_configure = (plugin_obj->configure_func != NULL);
	delete plugin_obj;
	g_module_close (module);
}

typedef bool (*stardict_virtualdict_plugin_init_func_t)(StarDictVirtualDictPlugInObject *obj);
typedef bool (*stardict_tts_plugin_init_func_t)(StarDictTtsPlugInObject *obj);
typedef bool (*stardict_misc_plugin_init_func_t)(void);

void StarDictPlugins::load_plugin(const char *filename)
{
	bool found = false;
	for (std::list<std::string>::iterator iter = loaded_plugin_list.begin(); iter != loaded_plugin_list.end(); ++iter) {
		if (*iter == filename) {
			found = true;
			break;
		}
	}
	if (found)
		return;
	loaded_plugin_list.push_back(filename);
	GModule *module;
	module = g_module_open (filename, G_MODULE_BIND_LAZY);
	if (!module) {
		g_print("Load %s failed!\n", filename);
		return;
	}
	union {
		stardict_plugin_init_func_t stardict_plugin_init;
		gpointer stardict_plugin_init_avoid_warning;
	} func;
	func.stardict_plugin_init = 0;
	if (!g_module_symbol (module, "stardict_plugin_init", (gpointer *)&(func.stardict_plugin_init_avoid_warning))) {
		g_print("Load %s failed: No stardict_plugin_init func!\n", filename);
		g_module_close (module);
		return;
	}
	StarDictPlugInObject *plugin_obj = new StarDictPlugInObject();
	bool failed = func.stardict_plugin_init(plugin_obj);
	if (failed) {
		g_print("Load %s failed!\n", filename);
		g_module_close (module);
		delete plugin_obj;
		return;
	}
	StarDictPlugInType ptype = plugin_obj->type;
	StarDictPluginBaseObject *baseobj = new StarDictPluginBaseObject(filename, module, plugin_obj->configure_func);
	delete plugin_obj;
	if (ptype == StarDictPlugInType_VIRTUALDICT) {
		union {
			stardict_virtualdict_plugin_init_func_t stardict_virtualdict_plugin_init;
			gpointer stardict_virtualdict_plugin_init_avoid_warning;
		} func2;
		func2.stardict_virtualdict_plugin_init = 0;
		if (!g_module_symbol (module, "stardict_virtualdict_plugin_init", (gpointer *)&(func2.stardict_virtualdict_plugin_init_avoid_warning))) {
			printf("Load %s failed: No stardict_virtualdict_plugin_init func!\n", filename);
			g_module_close (module);
			delete baseobj;
			return;
		}
		StarDictVirtualDictPlugInObject *virtualdict_plugin_obj = new StarDictVirtualDictPlugInObject();
		failed = func2.stardict_virtualdict_plugin_init(virtualdict_plugin_obj);
		if (failed) {
			g_print("Load %s failed!\n", filename);
			g_module_close (module);
			delete baseobj;
			delete virtualdict_plugin_obj;
			return;
		}
		VirtualDictPlugins.add(baseobj, virtualdict_plugin_obj);
	} else if (ptype == StarDictPlugInType_TTS) {
		union {
			stardict_tts_plugin_init_func_t stardict_tts_plugin_init;
			gpointer stardict_tts_plugin_init_avoid_warning;
		} func2;
		func2.stardict_tts_plugin_init = 0;
		if (!g_module_symbol (module, "stardict_tts_plugin_init", (gpointer *)&(func2.stardict_tts_plugin_init_avoid_warning))) {
			printf("Load %s failed: No stardict_tts_plugin_init func!\n", filename);
			g_module_close (module);
			delete baseobj;
			return;
		}
		StarDictTtsPlugInObject *tts_plugin_obj = new StarDictTtsPlugInObject();
		failed = func2.stardict_tts_plugin_init(tts_plugin_obj);
		if (failed) {
			g_print("Load %s failed!\n", filename);
			g_module_close (module);
			delete baseobj;
			delete tts_plugin_obj;
			return;
		}
		TtsPlugins.add(baseobj, tts_plugin_obj);
	} else if (ptype == StarDictPlugInType_MISC) {
		union {
			stardict_misc_plugin_init_func_t stardict_misc_plugin_init;
			gpointer stardict_misc_plugin_init_avoid_warning;
		} func3;
		func3.stardict_misc_plugin_init = 0;
		if (!g_module_symbol (module, "stardict_misc_plugin_init", (gpointer *)&(func3.stardict_misc_plugin_init_avoid_warning))) {
			printf("Load %s failed: No stardict_misc_plugin_init func!\n", filename);
			g_module_close (module);
			delete baseobj;
			return;
		}
		failed = func3.stardict_misc_plugin_init();
		if (failed) {
			g_print("Load %s failed!\n", filename);
			g_module_close (module);
			delete baseobj;
			return;
		}
		MiscPlugins.add(baseobj);
	} else {
		g_print("Load %s failed: Unknow type plugin!\n", filename);
		g_module_close (module);
		delete baseobj;
		return;
	}
}

void StarDictPlugins::unload_plugin(const char *filename, StarDictPlugInType plugin_type)
{
	for (std::list<std::string>::iterator iter = loaded_plugin_list.begin(); iter != loaded_plugin_list.end(); ++iter) {
		if (*iter == filename) {
			loaded_plugin_list.erase(iter);
			break;
		}
	}
	switch (plugin_type) {
		case StarDictPlugInType_VIRTUALDICT:
			VirtualDictPlugins.unload_plugin(filename);
			break;
		case StarDictPlugInType_TTS:
			TtsPlugins.unload_plugin(filename);
			break;
		case StarDictPlugInType_MISC:
			MiscPlugins.unload_plugin(filename);
			break;
		default:
			break;
	}
}

void StarDictPlugins::configure_plugin(const char *filename, StarDictPlugInType plugin_type)
{
	switch (plugin_type) {
		case StarDictPlugInType_VIRTUALDICT:
			VirtualDictPlugins.configure_plugin(filename);
			break;
		case StarDictPlugInType_TTS:
			TtsPlugins.configure_plugin(filename);
			break;
		case StarDictPlugInType_MISC:
			MiscPlugins.configure_plugin(filename);
			break;
		default:
			break;
	}
}

//
// class StarDictVirtualDictPlugins begin.
//

StarDictVirtualDictPlugins::StarDictVirtualDictPlugins()
{
}

StarDictVirtualDictPlugins::~StarDictVirtualDictPlugins()
{
	for (std::vector<StarDictVirtualDictPlugin *>::iterator i = oPlugins.begin(); i != oPlugins.end(); ++i) {
		delete *i;
	}
}

void StarDictVirtualDictPlugins::add(StarDictPluginBaseObject *baseobj, StarDictVirtualDictPlugInObject *virtualdict_plugin_obj)
{
	StarDictVirtualDictPlugin *plugin = new StarDictVirtualDictPlugin(baseobj, virtualdict_plugin_obj);
	oPlugins.push_back(plugin);
}

void StarDictVirtualDictPlugins::unload_plugin(const char *filename)
{
	for (std::vector<StarDictVirtualDictPlugin *>::iterator iter = oPlugins.begin(); iter != oPlugins.end(); ++iter) {
		if (strcmp((*iter)->get_filename(), filename) == 0) {
			delete *iter;
			oPlugins.erase(iter);
			break;
		}
	}
}

void StarDictVirtualDictPlugins::configure_plugin(const char *filename)
{
	for (std::vector<StarDictVirtualDictPlugin *>::iterator iter = oPlugins.begin(); iter != oPlugins.end(); ++iter) {
		if (strcmp((*iter)->get_filename(), filename) == 0) {
			(*iter)->configure();
			break;
		}
	}
}

void StarDictVirtualDictPlugins::lookup(size_t iPlugin, const gchar *word, char ***pppWord, char ****ppppWordData)
{
	oPlugins[iPlugin]->lookup(word, pppWord, ppppWordData);
}

const char *StarDictVirtualDictPlugins::dict_name(size_t iPlugin)
{
	return oPlugins[iPlugin]->dict_name();
}

const char *StarDictVirtualDictPlugins::dict_id(size_t iPlugin)
{
	return oPlugins[iPlugin]->get_filename();
}

bool StarDictVirtualDictPlugins::find_dict_by_id(const char *id, size_t &iPlugin)
{
	for (std::vector<StarDictVirtualDictPlugin *>::size_type i = 0; i < oPlugins.size(); i++) {
		if (strcmp(oPlugins[i]->get_filename(), id)==0) {
			iPlugin = i;
			return true;
		}
	}
	return false;
}

//
// class StarDictVirtualDictPlugin begin.
//

StarDictVirtualDictPlugin::StarDictVirtualDictPlugin(StarDictPluginBaseObject *baseobj_, StarDictVirtualDictPlugInObject *virtualdict_plugin_obj):
	StarDictPluginBase(baseobj_)
{
	obj = virtualdict_plugin_obj;
}

StarDictVirtualDictPlugin::~StarDictVirtualDictPlugin()
{
	delete obj;
}

void StarDictVirtualDictPlugin::lookup(const char *word, char ***pppWord, char ****ppppWordData)
{
	obj->lookup_func(word, pppWord, ppppWordData);
}

const char *StarDictVirtualDictPlugin::dict_name()
{
	return obj->dict_name;
}

//
// class StarDictTtsPlugins begin.
//

StarDictTtsPlugins::StarDictTtsPlugins()
{
}

StarDictTtsPlugins::~StarDictTtsPlugins()
{
	for (std::vector<StarDictTtsPlugin *>::iterator i = oPlugins.begin(); i != oPlugins.end(); ++i) {
		delete *i;
	}
}

void StarDictTtsPlugins::add(StarDictPluginBaseObject *baseobj, StarDictTtsPlugInObject *tts_plugin_obj)
{
	StarDictTtsPlugin *plugin = new StarDictTtsPlugin(baseobj, tts_plugin_obj);
	oPlugins.push_back(plugin);
}

void StarDictTtsPlugins::unload_plugin(const char *filename)
{
	for (std::vector<StarDictTtsPlugin *>::iterator iter = oPlugins.begin(); iter != oPlugins.end(); ++iter) {
		if (strcmp((*iter)->get_filename(), filename) == 0) {
			delete *iter;
			oPlugins.erase(iter);
			break;
		}
	}
}

void StarDictTtsPlugins::configure_plugin(const char *filename)
{
	for (std::vector<StarDictTtsPlugin *>::iterator iter = oPlugins.begin(); iter != oPlugins.end(); ++iter) {
		if (strcmp((*iter)->get_filename(), filename) == 0) {
			(*iter)->configure();
			break;
		}
	}
}

void StarDictTtsPlugins::saytext(size_t iPlugin, const gchar *text)
{
	oPlugins[iPlugin]->saytext(text);
}

const char *StarDictTtsPlugins::tts_name(size_t iPlugin)
{
	return oPlugins[iPlugin]->tts_name();
}

//
// class StarDictTtsPlugin begin.
//

StarDictTtsPlugin::StarDictTtsPlugin(StarDictPluginBaseObject *baseobj_, StarDictTtsPlugInObject *tts_plugin_obj):
	StarDictPluginBase(baseobj_)
{
	obj = tts_plugin_obj;
}

StarDictTtsPlugin::~StarDictTtsPlugin()
{
	delete obj;
}

void StarDictTtsPlugin::saytext(const char *text)
{
	obj->saytext_func(text);
}

const char *StarDictTtsPlugin::tts_name()
{
	return obj->tts_name;
}

//
// class StarDictMiscPlugins begin.
//

StarDictMiscPlugins::StarDictMiscPlugins()
{
}

StarDictMiscPlugins::~StarDictMiscPlugins()
{
	for (std::vector<StarDictMiscPlugin *>::iterator i = oPlugins.begin(); i != oPlugins.end(); ++i) {
		delete *i;
	}
}

void StarDictMiscPlugins::add(StarDictPluginBaseObject *baseobj)
{
	StarDictMiscPlugin *plugin = new StarDictMiscPlugin(baseobj);
	oPlugins.push_back(plugin);
}

void StarDictMiscPlugins::unload_plugin(const char *filename)
{
	for (std::vector<StarDictMiscPlugin *>::iterator iter = oPlugins.begin(); iter != oPlugins.end(); ++iter) {
		if (strcmp((*iter)->get_filename(), filename) == 0) {
			delete *iter;
			oPlugins.erase(iter);
			break;
		}
	}
}

void StarDictMiscPlugins::configure_plugin(const char *filename)
{
	for (std::vector<StarDictMiscPlugin *>::iterator iter = oPlugins.begin(); iter != oPlugins.end(); ++iter) {
		if (strcmp((*iter)->get_filename(), filename) == 0) {
			(*iter)->configure();
			break;
		}
	}
}

//
// class StarDictMiscPlugin begin.
//

StarDictMiscPlugin::StarDictMiscPlugin(StarDictPluginBaseObject *baseobj_):
	StarDictPluginBase(baseobj_)
{
}

StarDictMiscPlugin::~StarDictMiscPlugin()
{
}

void StarDictVirtualDictPlugins::reorder(const std::list<std::string>& order_list)
{
	std::vector<StarDictVirtualDictPlugin *> prev(oPlugins);
	oPlugins.clear();
	for (std::list<std::string>::const_iterator i = order_list.begin(); i != order_list.end(); ++i) {
		for (std::vector<StarDictVirtualDictPlugin *>::iterator j = prev.begin(); j != prev.end(); ++j) {
			if (*i == (*j)->get_filename()) {
				oPlugins.push_back(*j);
			}
		}
	}
	for (std::vector<StarDictVirtualDictPlugin *>::iterator i=prev.begin(); i!=prev.end(); ++i) {
		std::vector<StarDictVirtualDictPlugin *>::iterator j;
		for (j=oPlugins.begin(); j!=oPlugins.end(); ++j) {
			if (*j == *i)
				break;
		}
		if (j == oPlugins.end())
			delete *i;
	}
}

void StarDictTtsPlugins::reorder(const std::list<std::string>& order_list)
{
	std::vector<StarDictTtsPlugin *> prev(oPlugins);
	oPlugins.clear();
	for (std::list<std::string>::const_iterator i = order_list.begin(); i != order_list.end(); ++i) {
		for (std::vector<StarDictTtsPlugin *>::iterator j = prev.begin(); j != prev.end(); ++j) {
			if (*i == (*j)->get_filename()) {
				oPlugins.push_back(*j);
			}
		}
	}
	for (std::vector<StarDictTtsPlugin *>::iterator i=prev.begin(); i!=prev.end(); ++i) {
		std::vector<StarDictTtsPlugin *>::iterator j;
		for (j=oPlugins.begin(); j!=oPlugins.end(); ++j) {
			if (*j == *i)
				break;
		}
		if (j == oPlugins.end())
			delete *i;
	}
}

void StarDictMiscPlugins::reorder(const std::list<std::string>& order_list)
{
	std::vector<StarDictMiscPlugin *> prev(oPlugins);
	oPlugins.clear();
	for (std::list<std::string>::const_iterator i = order_list.begin(); i != order_list.end(); ++i) {
		for (std::vector<StarDictMiscPlugin *>::iterator j = prev.begin(); j != prev.end(); ++j) {
			if (*i == (*j)->get_filename()) {
				oPlugins.push_back(*j);
			}
		}
	}
	for (std::vector<StarDictMiscPlugin *>::iterator i=prev.begin(); i!=prev.end(); ++i) {
		std::vector<StarDictMiscPlugin *>::iterator j;
		for (j=oPlugins.begin(); j!=oPlugins.end(); ++j) {
			if (*j == *i)
				break;
		}
		if (j == oPlugins.end())
			delete *i;
	}
}
