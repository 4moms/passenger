#include <string>
#include <map>
#include <vector>
#include <utility>
#include <boost/make_shared.hpp>
#include <boost/shared_array.hpp>
#include <boost/bind.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>
#include <sys/types.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cassert>
#include <unistd.h>
#include <pthread.h>
#include <limits.h>  // for PTHREAD_STACK_MIN
#include <pwd.h>
#include <grp.h>
#include <dirent.h>
#include <ApplicationPool2/Process.h>
#include <ApplicationPool2/Options.h>
#include <ApplicationPool2/PipeWatcher.h>
#include <FileDescriptor.h>
#include <SafeLibev.h>
#include <Exceptions.h>
#include <ResourceLocator.h>
#include <StaticString.h>
#include <ServerInstanceDir.h>
#include <Utils/BufferedIO.h>
#include <Utils/ScopeGuard.h>
#include <Utils/Timer.h>
#include <Utils/IOUtils.h>
#include <Utils/StrIntUtils.h>
#include <Utils/Base64.h>

namespace wtf {
	using namespace std;
	using namespace boost;
	using namespace oxt;

	struct StupidOptions {
		string absolutePathToStartup;
		string user;
		string group;
		string defaultUser;
		string defaultGroup;
	};

	struct StartupInfo {
		bool switchUser;
		string username;
		string groupname;
		string home;
		string shell;
		uid_t uid;
		gid_t gid;
		int ngroups;
		shared_array<gid_t> gidset;
	};

	void testPrep(StartupInfo &info, StupidOptions &options) {
		string defaultGroup;
		string startupFile = options.absolutePathToStartup;
		struct passwd *userInfo = NULL;
		struct group *groupInfo = NULL;

		if (options.defaultGroup.empty()) {
			struct passwd *info = getpwnam(options.defaultUser.c_str());
			if (info == NULL) {
				throw "WTF: getpwnam(options.defaultUser) == NULL";
			}
			struct group *group = getgrgid(info->pw_gid);
			if (group == NULL) {
				throw "WTF: getgrgid(info->pw_gid) == NULL";
			}
			defaultGroup = group->gr_name;
		} else {
			defaultGroup = options.defaultGroup;
		}

		if (!options.user.empty()) {
			userInfo = getpwnam(options.user.c_str());
		} else {
			struct stat buf;
			if (::lstat(startupFile.c_str(), &buf) == -1) {
				int e = errno;
				throw "lstat(" + startupFile + ") failed";
			}
			userInfo = getpwuid(buf.st_uid);
		}
		if (userInfo == NULL || userInfo->pw_uid == 0) {
			userInfo = getpwnam(options.defaultUser.c_str());
		}

		if (!options.group.empty()) {
			if (options.group == "!STARTUP_FILE!") {
				struct stat buf;
				if (::lstat(startupFile.c_str(), &buf) == -1) {
					int e = errno;
					throw "lstat(" + startupFile + ") failed";
				}
				groupInfo = getgrgid(buf.st_gid);
			} else {
				groupInfo = getgrnam(options.group.c_str());
			}
		} else if (userInfo != NULL) {
			groupInfo = getgrgid(userInfo->pw_gid);
		}
		if (groupInfo == NULL || groupInfo->gr_gid == 0) {
			groupInfo = getgrnam(defaultGroup.c_str());
		}

		if (userInfo == NULL) {
			throw "No user";
		}
		if (groupInfo == NULL) {
			throw "No group";
		}

		#if !defined(gid_t) && defined(__APPLE__)
			int groups[1024];
			info.ngroups = sizeof(groups) / sizeof(int);
		#else
		#ifdef NGROUPS_MAX
			gid_t groups[NGROUPS_MAX];
		#else
			gid_t groups[1024];
		#endif
			info.ngroups = sizeof(groups) / sizeof(gid_t);
		#endif
		int ret;
		info.switchUser = true;
		info.username = userInfo->pw_name;
		info.groupname = groupInfo->gr_name;
		info.home = userInfo->pw_dir;
		info.shell = userInfo->pw_shell;
		info.uid = userInfo->pw_uid;
		info.gid = groupInfo->gr_gid;
		#if !defined(HAVE_GETGROUPLIST) && (defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__))
			#define HAVE_GETGROUPLIST
		#endif
		#ifdef HAVE_GETGROUPLIST
			ret = getgrouplist(userInfo->pw_name, groupInfo->gr_gid,
				groups, &info.ngroups);
			if (ret == -1) {
				int e = errno;
				throw "getgrouplist() failed";
			}
			info.gidset = shared_array<gid_t>(new gid_t[info.ngroups]);
			for (int i = 0; i < info.ngroups; i++) {
				info.gidset[i] = groups[i];
			}
		#endif

		cout << "User: " << userInfo->pw_name << endl;
		cout << "Group: " << defaultGroup << endl;
	}

	void testSwitch(const StartupInfo &info) {
		if (info.switchUser) {
#ifdef HAVE_GETGROUPLIST
			if (setgroups(info.ngroups, info.gidset.get()) == -1) {
				int e = errno;
				printf("!> Error\n");
				printf("!> \n");
				printf("setgroups() failed: %s (errno=%d)\n",
					strerror(e), e);
				fflush(stdout);
				_exit(1);
			}
#endif
		}
		cout << "Finished" << endl;
	}
}

using namespace std;
using namespace wtf;
int main(int argc, char** argv) {
	StupidOptions options;
	StartupInfo info;
	switch (argc) {
		case 4: options.defaultGroup = argv[3];
		case 3: options.defaultUser = argv[2];
		case 2: options.absolutePathToStartup = argv[1];
			break;
		default:
			cout << "Usage: " << argv[0] << " path-to-file [user [group]]" << endl;
			cout << "Argc: " << argc << endl;
			return 1;
	}
	testPrep(info, options);
	testSwitch(info);
	return 0;
}
