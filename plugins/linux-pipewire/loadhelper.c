#include <obs-module.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <fcntl.h>
#include <unistd.h>

static bool is_flatpak_sandbox(void)
{
	static bool flatpak_info_exists = false;
	static bool initialized = false;

	if (!initialized) {
		flatpak_info_exists = access("/.flatpak-info", F_OK) == 0;
		initialized = true;
	}

	return flatpak_info_exists;
}

static int run_command(const char *command)
{
	struct dstr str;
	int result;

	dstr_init_copy(&str, "PATH=\"$PATH:/sbin\" ");

	if (is_flatpak_sandbox())
		dstr_cat(&str, "flatpak-spawn --host ");

	dstr_cat(&str, command);
	result = system(str.array);
	dstr_free(&str);
	return result;
}

static bool loopback_module_loaded()
{
	bool loaded = false;

	char temp[512];

	FILE *fp = fopen("/proc/modules", "r");

	if (!fp)
		return false;

	while (fgets(temp, sizeof(temp), fp)) {
		if (strstr(temp, "v4l2loopback")) {
			loaded = true;
			break;
		}
	}

	if (fp)
		fclose(fp);

	return loaded;
}

bool loopback_module_available()
{
	if (loopback_module_loaded()) {
		return true;
	}

	if (run_command("modinfo v4l2loopback >/dev/null 2>&1") == 0) {
		return true;
	}

	return false;
}
