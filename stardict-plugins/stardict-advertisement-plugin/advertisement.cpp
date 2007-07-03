#include "advertisement.h"
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <string>
#include <map>
#include <list>
#include <vector>


static const StarDictPluginSystemInfo *plugin_info = NULL;
static std::string datapath;

struct DictEntry {
	std::string word;
	std::list<char *> datalist;
};

static std::multimap<std::string, DictEntry> dict_map;
static std::list<char *> dictdata_list;

static void my_strstrip(gchar *str)
{
	char *p1, *p2;
	p1=str;
	p2=str;
	while (*p1 != '\0') {
		if (*p1 == '\\') {
			p1++;
			if (*p1 == 'n') {
				*p2='\n';
				p2++;
				p1++;
				continue;
			} else if (*p1 == '\\') {
				*p2='\\';
				p2++;
				p1++;
				continue;
			} else {
				*p2='\\';
				p2++;
				*p2=*p1;
				p2++;
				p1++;
				continue;
			}
		} else {
			*p2 = *p1;
			p2++;
			p1++;
			continue;
		}
	}
	*p2 = '\0';
}

static char *build_dictdata(char type, const char *definition)
{
	guint32 size;
	char *data;
	if (g_ascii_isupper(type)) {
		if (type == 'P') {
			std::string filename;
#ifdef _WIN32
			if (definition[0] != '\0' && definition[1] == ':') {
#else
			if (definition[0] == '/') {
#endif
				filename = definition;
			} else {
				filename = datapath + G_DIR_SEPARATOR_S + definition;
			}
			struct stat stats;
			FILE *file;
			if (g_stat (filename.c_str(), &stats) == 0 && (file = g_fopen(filename.c_str(), "r"))!=NULL) {
				size = sizeof(char) + sizeof(guint32) + stats.st_size;
				data = (char *)g_malloc(sizeof(guint32) + size);
				char *p = data;
				*((guint32 *)p) = size;
				p += sizeof(guint32);
				*p = type;
				p++;
				*((guint32 *)p) = g_htonl(stats.st_size);
				p += sizeof(guint32);
				fread(p, 1, stats.st_size, file);
				fclose(file);
				return data;
			}
		}
		size = sizeof(char) + sizeof(guint32) + 0;
		data = (char *)g_malloc(sizeof(guint32) + size);
		char *p = data;
		*((guint32 *)p) = size;
		p += sizeof(guint32);
		*p = type;
		p++;
		*((guint32 *)p) = g_htonl(0);
		return data;
	} else {
		size_t len = strlen(definition);
		size = sizeof(char) + len + 1;
		data = (char *)g_malloc(sizeof(guint32) + size);
		char *p = data;
		*((guint32 *)p) = size;
		p += sizeof(guint32);
		*p = type;
		p++;
		memcpy(p, definition, len+1);
		return data;
	}
}

static bool load_dict(const char *filename)
{
	gchar *contents;
	gboolean success = g_file_get_contents(filename, &contents, NULL, NULL);
	if (!success) {
		g_print("File %s doesn't exist!\n", filename);
		return true;
	}
	gchar *p, *p1, *p2;
	p = contents;
	int step = 0;
	std::list<std::string> wordlist;
	DictEntry dictentry;
	char dict_type = 'm';
	while (true) {
		p1 = strchr(p, '\n');
		if (!p1)
			break;
		*p1 = '\0';
		if (step == 0) { // Word list.
			if (*p == '\0')
				break;
			wordlist.clear();
			while (true) {
				p2 = strchr(p, '\t');
				if (!p2)
					break;
				*p2 = '\0';
				wordlist.push_back(p);
				p = p2 +1;
			}
			wordlist.push_back(p);
			dictentry.datalist.clear();
			step = 1;
		} else if (step == 1) { // Type
			dict_type = *p;
			step = 2;
		} else if (step == 2) { // Definition
			my_strstrip(p);
			char *data = build_dictdata(dict_type, p);
			dictentry.datalist.push_back(data);
			dictdata_list.push_back(data);
			step = 3;
		} else {
			if (*p == '\0') { // Blank line.
				for (std::list<std::string>::iterator i = wordlist.begin(); i != wordlist.end(); ++i) {
					dictentry.word = *i;
					gchar *lower_str = g_utf8_strdown(dictentry.word.c_str(), dictentry.word.length());
					dict_map.insert(std::pair<std::string, DictEntry>(lower_str, dictentry));
					g_free(lower_str);
				}
				step = 0;
			} else { // Same as step 1.
				dict_type = *p;
				step = 2;
			}
		}
		p = p1 + 1;
	}
	g_free(contents);
	return false;
}

static void unload_dict()
{
	for (std::list<char *>::iterator i = dictdata_list.begin(); i != dictdata_list.end(); ++i) {
		g_free(*i);
	}
}

static void lookup(const char *word, char ***pppWord, char ****ppppWordData)
{
	gchar *lower_str = g_utf8_strdown(word, -1);
	std::multimap<std::string, DictEntry>::iterator iter = dict_map.find(lower_str);
	if (iter == dict_map.end()) {
		*pppWord = NULL;
	} else {
		std::vector< std::pair<std::string, std::list<char *> > > result;
		do {
			result.push_back(std::pair<std::string, std::list<char *> >(iter->second.word, iter->second.datalist));
			iter++;
		} while (iter != dict_map.upper_bound(lower_str));
		*pppWord = (gchar **)g_malloc(sizeof(gchar *)*(result.size()+1));
		*ppppWordData = (gchar ***)g_malloc(sizeof(gchar **)*result.size());
		for (std::vector< std::pair<std::string, std::list<char *> > >::size_type i = 0; i< result.size(); i++) {
			(*pppWord)[i] = g_strdup(result[i].first.c_str());
			std::list<char *> &datalist = result[i].second;
			(*ppppWordData)[i] = (gchar **)g_malloc(sizeof(gchar *)*(datalist.size()+1));
			int j = 0;
			for (std::list<char *>::iterator it = datalist.begin(); it != datalist.end(); ++it) {
				(*ppppWordData)[i][j] = (char *)g_memdup(*it, sizeof(guint32) + *reinterpret_cast<const guint32 *>(*it));
				j++;
			}
			(*ppppWordData)[i][j] = NULL;
		}
		(*pppWord)[result.size()] = NULL;
	}
	g_free(lower_str);
}

static void configure()
{
	g_print("Advertisement configure.\n");
}

bool stardict_plugin_init(StarDictPlugInObject *obj)
{
	if (strcmp(obj->version_str, PLUGIN_SYSTEM_VERSION)!=0) {
		g_print("Error: Advertisement plugin version doesn't match!\n");
		return true;
	}
	obj->type = StarDictPlugInType_VIRTUALDICT;
	obj->info_xml = g_strdup_printf("<plugin_info><name>%s</name><version>1.0</version><short_desc>%s</short_desc><long_desc>%s</long_desc><author>Hu Zheng &lt;huzheng_001@163.com&gt;</author><website>http://stardict.sourceforge.net</website></plugin_info>", _("Advertisement"), _("Advertisement virtual dictionary."), _("Show advertisement and the user dictionary."));
	obj->configure_func = configure;
	plugin_info = obj->plugin_info;

	return false;
}

void stardict_plugin_exit(void)
{
	unload_dict();
}

bool stardict_virtualdict_plugin_init(StarDictVirtualDictPlugInObject *obj)
{
	obj->lookup_func = lookup;
	obj->dict_name = _("Advertisement");
	datapath = plugin_info->datadir;
	datapath += G_DIR_SEPARATOR_S "data" G_DIR_SEPARATOR_S "advertisement";
	bool failed = load_dict((datapath + G_DIR_SEPARATOR_S "advertisement.txt").c_str());
	if (failed)
		return true;
	g_print(_("Advertisement plug-in loaded.\n"));
	return false;
}
