// Main.cpp : This file contains the 'main' function. Program
// execution begins and ends there.
//

#include "stdafx.h"
#include "Utils.h"
#include "Reports.h"
#include "Scanner.h"
#include "Remediate.h"

#include "Version.info"


#define ARGX3(s1, s2, s3) \
  (!_wcsicmp(argv[i], s1) || !_wcsicmp(argv[i], s2) || !_wcsicmp(argv[i], s3))
#define ARG(S) ARGX3(L"-" #S, L"--" #S, L"/" #S)
#define ARGPARAMCOUNT(X) ((i + X) <= (argc - 1))


struct CCommandLineOptions {  
  bool remediateSig{};
  bool report{};
  bool report_pretty{};
  bool verbose{};
  bool no_logo{};
  bool help{};  
};

CCommandLineOptions cmdline_options;

__inline std::vector<wchar_t> FormatTimestamp(uint64_t timestamp) {
  std::vector<wchar_t> buf(64, L'\0');
  struct tm* tm = std::localtime(reinterpret_cast<time_t*>(&timestamp));
  wcsftime(buf.data(), buf.size() - 1, L"%FT%T%z", tm);

  return buf;
}

DWORD PrintHelp(int32_t argc, wchar_t* argv[]) {
  DWORD rv{ ERROR_SUCCESS };
  
  wprintf(L"/remediate_sig\n");
  wprintf(L"  Remove JndiLookup.class from JAR, WAR, EAR, ZIP files detected by scanner utility\n");
  wprintf(L"/report\n");
  wprintf(L"  Generate a JSON for mitigations of supported CVE(s).\n");
  wprintf(L"/report_pretty\n");
  wprintf(L"  Generate a pretty JSON for mitigations of supported CVE(s).\n");
  wprintf(L"\n");

  return rv;
}

int32_t ProcessCommandLineOptions(int32_t argc, wchar_t* argv[]) {
  int32_t rv = ERROR_SUCCESS;

  for (int32_t i = 1; i < argc; i++) {
    if (0) {
    }    
    else if (ARG(remediate_sig)) {
      cmdline_options.remediateSig = true;
      cmdline_options.no_logo = true;
    }
    else if (ARG(report)) {
      cmdline_options.report = true;
    }
    else if (ARG(report_pretty)) {
      cmdline_options.report = true;
      cmdline_options.report_pretty = true;
    }
    else if (ARG(nologo)) {
      cmdline_options.no_logo = true;
    }
    else if (ARG(v) || ARG(verbose)) {
      cmdline_options.verbose = true;
    }
    else if (ARG(? ) || ARG(h) || ARG(help)) {
      cmdline_options.help = true;
    }
  }
  
  return rv;
}

DWORD SetPrivilege(
    HANDLE hToken,          // access token handle
    const std::wstring& Privilege,  // name of privilege to enable/disable
    bool EnablePrivilege   // to enable or disable privilege
)
{
    TOKEN_PRIVILEGES tp = { 0 };
    LUID luid = { 0 };

    DWORD status = ERROR_SUCCESS;

    if (!LookupPrivilegeValue(
        nullptr,            // lookup privilege on local system
        Privilege.c_str(),   // privilege to lookup 
        &luid))        // receives LUID of privilege
    {
        status = GetLastError();
        LOG_WIN32_MESSAGE(status, L"%s", L"Failed to get privilege");
        return status;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = EnablePrivilege ? SE_PRIVILEGE_ENABLED : 0;

    // Enable the privilege or disable all privileges.

    if (!AdjustTokenPrivileges(
        hToken,
        FALSE,
        &tp,
        sizeof(TOKEN_PRIVILEGES),
        (PTOKEN_PRIVILEGES)nullptr,
        (PDWORD)nullptr))
    {
        status = GetLastError();
        LOG_WIN32_MESSAGE(status, L"%s", L"Failed to set privilege");
        return status;
    }
    
    status = GetLastError();
    if (status == ERROR_NOT_ALL_ASSIGNED)
    {
        LOG_WIN32_MESSAGE(status, L"%s", L"The token does not have the specified privilege");
        return status;
    }

    return status;
}

int32_t __cdecl wmain(int32_t argc, wchar_t* argv[]) {
  int32_t rv = ERROR_SUCCESS;

  SetUnhandledExceptionFilter(CatchUnhandledExceptionFilter);
  _setmode(_fileno(stdout), _O_U16TEXT);

  HANDLE hToken = nullptr;
  // Open a handle to the access token for the calling process.
  if (!OpenProcessToken(GetCurrentProcess(),
      TOKEN_ADJUST_PRIVILEGES,
      &hToken))
  {
      LOG_WIN32_MESSAGE(GetLastError(), L"%s", L"Failed to open process token");
      goto END;
  }

  // Enable the SE_TAKE_OWNERSHIP_NAME privilege.
  if (SetPrivilege(hToken, SE_TAKE_OWNERSHIP_NAME, TRUE) != ERROR_SUCCESS)
  {
      LOG_MESSAGE("You must be logged on as Administrator");
      CloseHandle(hToken);
      goto END;
  }

  if (SetPrivilege(hToken, SE_RESTORE_NAME, TRUE) != ERROR_SUCCESS)
  {
      LOG_MESSAGE("You must be logged on as Administrator");
      CloseHandle(hToken);
      goto END;
  }

  CloseHandle(hToken);

#ifndef _WIN64
  using typeWow64DisableWow64FsRedirection = BOOL(WINAPI*)(PVOID OlValue);
  typeWow64DisableWow64FsRedirection Wow64DisableWow64FsRedirection;
  BOOL bIs64BitWindows = FALSE;
  PVOID pHandle{};

  if (!IsWow64Process(GetCurrentProcess(), &bIs64BitWindows)) {
    wprintf(L"Failed to determine if process is running as WoW64.\n");
    goto END;
  }

  if (bIs64BitWindows) {
    Wow64DisableWow64FsRedirection =
      (typeWow64DisableWow64FsRedirection)GetProcAddress(
        GetModuleHandle(L"Kernel32.DLL"), "Wow64DisableWow64FsRedirection");

    if (Wow64DisableWow64FsRedirection) {
      Wow64DisableWow64FsRedirection(&pHandle);
    }
  }
#endif

  rv = ProcessCommandLineOptions(argc, argv);
  if (ERROR_SUCCESS != rv) {
    wprintf(L"Failed to process command line options.\n");
    goto END;
  }

  if (!cmdline_options.no_logo) {
    wprintf(L"Qualys Log4j Remediation Utility %S\n", REMEDIATE_VERSION_STRING);
    wprintf(L"https://www.qualys.com/\n");
    wprintf(L"Supported CVE(s): CVE-2021-44228, CVE-2021-45046\n\n");
  }

  if (cmdline_options.help) {
    PrintHelp(argc, argv);
    goto END;
  }

  if (cmdline_options.remediateSig) {
    OpenStatusFile(GetRemediationStatusFilename());
  }

  remSummary.scanStart = std::time(nullptr);

  if (cmdline_options.remediateSig) {
    LOG_MESSAGE(L"Remediation start time : %s", FormatTimestamp(remSummary.scanStart).data());
  }

  // Command handlers
  if (cmdline_options.remediateSig) {
    rv = log4jremediate::RemediateLog4JSigReport::RemediateFromSignatureReport();
    if (rv != ERROR_SUCCESS) {
      LOG_MESSAGE(L"Failed to remediate vulnerabilities from signature report.");
    }
  }  

  remSummary.scanEnd = std::time(nullptr);

  if (cmdline_options.remediateSig) {
    LOG_MESSAGE(L"Remediation end time : %s", FormatTimestamp(remSummary.scanEnd).data());
  }

  if (!cmdline_options.no_logo) {
    wprintf(L"\tRemediation Summary:\n");
    wprintf(L"\tRemediation Date:\t\t %s\n", FormatTimestamp(remSummary.scanEnd).data());
    wprintf(L"\tRemediation Duration:\t\t %llu Seconds\n", remSummary.scanEnd - remSummary.scanStart);    
  }

  if (cmdline_options.report) {
    GenerateRemediationJSONReport(cmdline_options.report_pretty);
  }

END:

  if (cmdline_options.remediateSig) {
    if (rv == ERROR_SUCCESS) {
      LOG_MESSAGE(L"\nRun status : Success");
      LOG_MESSAGE(L"Result file location : %s", GetRemediationReportFilename().c_str());
    }
    else {
      LOG_MESSAGE(L"\nRun status : Partially Successful");
      LOG_MESSAGE(L"Result file location : %s", GetRemediationReportFilename().c_str());      
    }
  }

  CloseStatusFile();

  return rv;
}