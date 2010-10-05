/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla XULRunner.
 *
 * The Initial Developer of the Original Code is
 * Benjamin Smedberg <benjamin@smedbergs.us>
 *
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Mozilla Foundation <http://www.mozilla.org/>. All Rights Reserved.
 *
 * Contributor(s):
 * Matthew Gertner <matthew.gertner@gmail.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include <stdio.h>
#include <stdarg.h>

#ifdef XP_WIN
#include <windows.h>
#include <io.h>
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#define PATH_SEPARATOR_CHAR '\\'
#define R_OK 04
#elif defined(XP_MACOSX)
#include <CFBundle.h>
#include <unistd.h>
#include <sys/stat.h>
#define PATH_SEPARATOR_CHAR '/'
#elif defined (XP_OS2)
#define INCL_DOS
#define INCL_DOSMISC
#define INCL_DOSERRORS
#include <os2.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#define PATH_SEPARATOR_CHAR '\\'
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#define PATH_SEPARATOR_CHAR '/'
#endif

#ifdef XP_WIN
#include "nsWindowsWMain.cpp"
#endif

#ifdef XP_BEOS
#include <Entry.h>
#include <Path.h>
#endif

#include "nsXPCOMGlue.h"
#include "nsINIParser.h"
#include "prtypes.h"
#include "nsXPCOMPrivate.h" // for XP MAXPATHLEN
#include "nsMemory.h" // for NS_ARRAY_LENGTH
#include "nsXULAppAPI.h"
#include "nsILocalFile.h"

#define VERSION_MAXLEN 128

static void Output(PRBool isError, const char *fmt, ... )
{
  va_list ap;
  va_start(ap, fmt);

#if defined(XP_WIN) && !MOZ_WINCONSOLE
  char msg[2048];

  vsnprintf(msg, sizeof(msg), fmt, ap);

  UINT flags = MB_OK;
  if (isError)
    flags |= MB_ICONERROR;
  else
    flags |= MB_ICONINFORMATION;
  MessageBox(NULL, msg, "XULRunner", flags);
#else
  vfprintf(stderr, fmt, ap);
#endif

  va_end(ap);
}

class AutoAppData
{
public:
  AutoAppData(nsILocalFile* aINIFile) : mAppData(nsnull) {
    nsresult rv = XRE_CreateAppData(aINIFile, &mAppData);
    if (NS_FAILED(rv))
      mAppData = nsnull;
  }
  ~AutoAppData() {
    if (mAppData)
      XRE_FreeAppData(mAppData);
  }

  operator nsXREAppData*() const { return mAppData; }
  nsXREAppData* operator -> () const { return mAppData; }

private:
  nsXREAppData* mAppData;
};

PRBool gEnvSet = PR_FALSE;

PRBool PR_CALLBACK SetEnvironmentVariable(const char* aString, const char* aValue, void* aClosure)
{
  gEnvSet = PR_TRUE;

#if defined(XP_WIN)
  char buffer[4096];
  sprintf(buffer, "%s=%s", aString, aValue);
  putenv(buffer);
#else
  // Check whether we are setting a variable that needs to support relative paths
  char resolvedPath[MAXPATHLEN];
  if (aValue[0] != '/' && aClosure &&
    (!strcmp(aString, "GRE_HOME") || !strcmp(aString, "PRISM_HOME") || !strcmp(aString, "XRE_PROFILE_PATH"))) {
    char* basePath = (char *) aClosure;
    char path[MAXPATHLEN];
    strcpy(path, basePath);
    // Get the path relative to the parent of the base path, since the base path is the full path of application.ini including the filename component
    strcat(path, "/../");
    strcat(path, aValue);
    if (realpath(path, resolvedPath)) {
      aValue = resolvedPath;
    }
  }

  setenv(aString, aValue, 1);
#endif

  return PR_TRUE;
}

XRE_CreateAppDataType XRE_CreateAppData;
XRE_FreeAppDataType XRE_FreeAppData;
XRE_mainType XRE_main;

int
main(int argc, char **argv)
{
  nsresult rv;
  char *lastSlash;

  char iniPath[MAXPATHLEN];
  char tmpPath[MAXPATHLEN];
  char webappPath[MAXPATHLEN] = { 0 };
  char overridePath[MAXPATHLEN] = { 0 };
  char greDir[MAXPATHLEN];
  PRBool greFound = PR_FALSE;
  char** newArgv = nsnull;
  int newArgc = 0;
  char* overrideFlag = "-override";
  char *webappFlag = "-webapp";
  
  // Check for -override and find webapp home if there isn't one.
  int i;
  for (i=0; i<argc; i++) {
    if (strcmp(argv[i], overrideFlag) == 0) {
      if (i < argc-1) {
        // -override without value will report an error in XRE_main
        strcpy(overridePath, argv[i+1]);
      }
    }
    else if (strcmp(argv[i], webappFlag) == 0) {
      if (i < argc-1) {
        // -webapp without value will report an error in nsCommandLineHandler.js
        strcpy(webappPath, argv[i+1]);
      }
    }
  }

  if (strlen(webappPath) > 0) {
    // Set the environment variable in case we restart without preserving command-line args
    // (e.g. extension install or application update).
    SetEnvironmentVariable("PRISM_WEBAPP", webappPath, nsnull);
  }
  else {
    // Check whether the variable is already set
#if defined(XP_WIN)
    ::GetEnvironmentVariableA("PRISM_WEBAPP", webappPath, MAXPATHLEN);
#else
    const char* prismWebapp = getenv("PRISM_WEBAPP");
    if (prismWebapp) {
      strcpy(webappPath, prismWebapp);
    }
#endif
  }
  
  if (strlen(overridePath) == 0 && strlen(webappPath) > 0) {
    // Check for override.ini in the webapp home.
    strcpy(overridePath, webappPath);
    int len = strlen(overridePath);
    if (overridePath[len-1] != PATH_SEPARATOR_CHAR) {
      overridePath[len++] = PATH_SEPARATOR_CHAR;
    }
    strncpy(overridePath+len, "override.ini", sizeof(overridePath)-(len));
    
    if (access(overridePath, R_OK) != 0) {
      // No override.ini there
      overridePath[0] = '\0';
    }
  }

#if defined(XP_MACOSX)
  CFBundleRef appBundle = CFBundleGetMainBundle();
  if (!appBundle)
    return 1;

  CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(appBundle);
  if (!resourcesURL)
    return 1;

  CFURLRef absResourcesURL = CFURLCopyAbsoluteURL(resourcesURL);
  CFRelease(resourcesURL);
  if (!absResourcesURL)
    return 1;

  CFURLRef iniFileURL =
    CFURLCreateCopyAppendingPathComponent(kCFAllocatorDefault,
                                          absResourcesURL,
                                          CFSTR("application.ini"),
                                          false);
  CFURLRef webappRootURL = 
    CFURLCreateCopyAppendingPathComponent(kCFAllocatorDefault,
                                          absResourcesURL,
                                          CFSTR("webapp"),
                                          false);
  CFURLRef overrideFileURL =
    CFURLCreateCopyAppendingPathComponent(kCFAllocatorDefault,
                                          webappRootURL,
                                          CFSTR("override.ini"),
                                          false);
 
  CFRelease(webappRootURL);
  CFRelease(absResourcesURL);
  if (!iniFileURL)
    return 1;

  CFStringRef iniPathStr =
    CFURLCopyFileSystemPath(iniFileURL, kCFURLPOSIXPathStyle);
  CFRelease(iniFileURL);
  if (!iniPathStr)
    return 1;

  CFStringGetCString(iniPathStr, iniPath, sizeof(iniPath),
                     kCFStringEncodingUTF8);
  CFRelease(iniPathStr);
  
  if ((strlen(overridePath) == 0) && overrideFileURL) {
    // Check whether the file exists
    FSRef fsRef;
  	CFURLGetFSRef(overrideFileURL, &fsRef);
    if (FSGetCatalogInfo(&fsRef, kFSCatInfoNone, NULL, NULL, NULL, NULL ) == noErr) {
      // Add to XRE_main arguments
      CFStringRef overridePathStr =
        CFURLCopyFileSystemPath(overrideFileURL, kCFURLPOSIXPathStyle);
      CFRelease(overrideFileURL);
      if (overridePathStr) {
        CFStringGetCString(overridePathStr, overridePath, sizeof(overridePath),
                           kCFStringEncodingUTF8);
        CFRelease(overridePathStr);
      }
    }
  }

#else

#ifdef XP_WIN
  if (!::GetModuleFileName(NULL, iniPath, sizeof(iniPath)))
    return 1;

#elif defined(XP_OS2)
   PPIB ppib;
   PTIB ptib;

   DosGetInfoBlocks(&ptib, &ppib);
   DosQueryModuleName(ppib->pib_hmte, sizeof(iniPath), iniPath);

#elif defined(XP_BEOS)
   BEntry e((const char *)argv[0], true); // traverse symlink
   BPath p;
   status_t err;
   err = e.GetPath(&p);
   NS_ASSERTION(err == B_OK, "realpath failed");

   if (err == B_OK)
     // p.Path returns a pointer, so use strcpy to store path in iniPath
     strcpy(iniPath, p.Path());

#else
  // on unix, there is no official way to get the path of the current binary.
  // instead of using the MOZILLA_FIVE_HOME hack, which doesn't scale to
  // multiple applications, we will try a series of techniques:
  //
  // 1) use realpath() on argv[0], which works unless we're loaded from the
  //    PATH
  // 2) manually walk through the PATH and look for ourself
  // 3) give up

  struct stat fileStat;

  if (!realpath(argv[0], iniPath) || stat(iniPath, &fileStat)) {
    const char *path = getenv("PATH");
    if (!path)
      return 1;

    char *pathdup = strdup(path);
    if (!pathdup)
      return 1;

    PRBool found = PR_FALSE;
    char *token = strtok(pathdup, ":");
    while (token) {
      sprintf(tmpPath, "%s/%s", token, argv[0]);
      if (realpath(tmpPath, iniPath) && stat(iniPath, &fileStat) == 0) {
        found = PR_TRUE;
        break;
      }
      token = strtok(NULL, ":");
    }
    free (pathdup);
    if (!found)
      return 1;
  }
#endif

  lastSlash = strrchr(iniPath, PATH_SEPARATOR_CHAR);
  if (!lastSlash)
    return 1;

  *(++lastSlash) = '\0';

  // On Linux/Win, look for XULRunner in appdir/xulrunner

  snprintf(greDir, sizeof(greDir),
           "%sxulrunner" XPCOM_FILE_PATH_SEPARATOR XPCOM_DLL,
           iniPath);

  greFound = (access(greDir, R_OK) == 0);

  strncpy(lastSlash, "application.ini", sizeof(iniPath) - (lastSlash - iniPath));

#endif

  nsINIParser parser;
  rv = parser.Init(iniPath);
  if (NS_FAILED(rv)) {
    fprintf(stderr, "Could not read application.ini\n");
    return 1;
  }
  
  // Register any environment variables. This is Prism-specific.
  parser.GetStrings("Environment", SetEnvironmentVariable, iniPath);
  if (!gEnvSet && strlen(overridePath) > 0) {
    // Look in override.ini as well
    nsINIParser overrideParser;
    if (NS_SUCCEEDED(overrideParser.Init(overridePath))) {
      overrideParser.GetStrings("Environment", SetEnvironmentVariable, iniPath);
    }
  }

  if (!greFound) {
    char minVersion[VERSION_MAXLEN];

    // If a gecko maxVersion is not specified, we assume that the app uses only
    // frozen APIs, and is therefore compatible with any xulrunner 1.x.
    char maxVersion[VERSION_MAXLEN] = "1.*";

    GREVersionRange range = {
      minVersion,
      PR_TRUE,
      maxVersion,
      PR_TRUE
    };

    rv = parser.GetString("Gecko", "MinVersion", minVersion, sizeof(minVersion));
    if (NS_FAILED(rv)) {
      fprintf(stderr,
              "The application.ini does not specify a [Gecko] MinVersion\n");
      return 1;
    }

    rv = parser.GetString("Gecko", "MaxVersion", maxVersion, sizeof(maxVersion));
    if (NS_SUCCEEDED(rv))
      range.upperInclusive = PR_TRUE;

    static const GREProperty kProperties[] = {
      { "xulrunner", "true" }
    };

    rv = GRE_GetGREPathWithProperties(&range, 1,
                                      kProperties, NS_ARRAY_LENGTH(kProperties),
                                      greDir, sizeof(greDir));
    if (NS_FAILED(rv)) {
      // XXXbsmedberg: Do something much smarter here: notify the
      // user/offer to download/?

      Output(PR_FALSE,
             "Could not find compatible GRE between version %s and %s.\n",
             range.lower, range.upper);
      return 1;
    }
  }
#ifdef XP_OS2
  // On OS/2 we need to set BEGINLIBPATH to be able to find XULRunner DLLs
  strcpy(tmpPath, greDir);
  lastSlash = strrchr(tmpPath, PATH_SEPARATOR_CHAR);
  if (lastSlash) {
    *lastSlash = '\0';
  }
  DosSetExtLIBPATH(tmpPath, BEGIN_LIBPATH);
#endif

  rv = XPCOMGlueStartup(greDir);
  if (NS_FAILED(rv)) {
    Output(PR_TRUE, "Couldn't load XPCOM.\n");
    return 1;
  }

  static const nsDynamicFunctionLoad kXULFuncs[] = {
    { "XRE_CreateAppData", (NSFuncPtr*) &XRE_CreateAppData },
    { "XRE_FreeAppData", (NSFuncPtr*) &XRE_FreeAppData },
    { "XRE_main", (NSFuncPtr*) &XRE_main },
    { nsnull, nsnull }
  };

  rv = XPCOMGlueLoadXULFunctions(kXULFuncs);
  if (NS_FAILED(rv)) {
    Output(PR_TRUE, "Couldn't load XRE functions.\n");
    return 1;
  }

  NS_LogInit();

  int retval;

  { // Scope COMPtr and AutoAppData
    nsCOMPtr<nsILocalFile> iniFile;
    rv = NS_NewNativeLocalFile(nsDependentCString(iniPath), PR_FALSE,
                               getter_AddRefs(iniFile));
    if (NS_FAILED(rv)) {
      Output(PR_TRUE, "Couldn't find application.ini file.\n");
      return 1;
    }

    AutoAppData appData(iniFile);
    if (!appData) {
      Output(PR_TRUE, "Error: couldn't parse application.ini.\n");
      return 1;
    }

    NS_ASSERTION(appData->directory, "Failed to get app directory.");

    if (!appData->xreDirectory) {
      // chop "libxul.so" off the GRE path
      lastSlash = strrchr(greDir, PATH_SEPARATOR_CHAR);
      if (lastSlash) {
        *lastSlash = '\0';
      }
      NS_NewNativeLocalFile(nsDependentCString(greDir), PR_FALSE,
                            &appData->xreDirectory);
    }
    
#if defined(XP_MACOSX)
    nsAutoString appBundlePath;
    appData->directory->GetPath(appBundlePath);
    setenv("PRISM_APP_BUNDLE", NS_ConvertUTF16toUTF8(appBundlePath).get(), 1);
    
    const char* prismHome = getenv("PRISM_HOME");
    if (prismHome) {
      NS_NewNativeLocalFile(nsCString(prismHome), PR_FALSE, &appData->directory);
    }
#endif

    // If we have an override path, copy it into the command-line args
    if (strlen(overridePath) > 0) {
      newArgv = new char*[argc+3];
      newArgv[0] = argv[0];
      newArgv[1] = overrideFlag;
      newArgv[2] = overridePath;

      for (i=1; i<argc; i++) {
        newArgv[i+2] = argv[i];
      }
      
      newArgc = argc+2;
      newArgv[newArgc] = nsnull;
    }

    retval = XRE_main(newArgc ? newArgc : argc, newArgv ? newArgv : argv, appData);
    
    if (newArgv) {
      delete newArgv;
    }
  }

  NS_LogTerm();

  XPCOMGlueShutdown();

  return retval;
}
