/*
Copyright (c) 2011 Roger Light <roger@atchoo.org>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. Neither the name of mosquitto nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef WIN32

#include <windows.h>

extern int run;
static SERVICE_STATUS_HANDLE service_handle;
int main(int argc, char *argv[]);

/* Service control callback */
void __stdcall ServiceHandler(DWORD fdwControl)
{
	switch(fdwControl){
		case SERVICE_CONTROL_CONTINUE:
			/* Continue from Paused state. */
			break;
		case SERVICE_CONTROL_PAUSE:
			/* Pause service. */
			break;
		case SERVICE_CONTROL_SHUTDOWN:
			/* System is shutting down. */
		case SERVICE_CONTROL_STOP:
			/* Service should stop. */
			run = 0;
			break;
	}
}

/* Function called when started as a service. */
void __stdcall ServiceMain(DWORD dwArgc, LPTSTR *lpszArgv)
{
	char **argv;
	int argc = 1;
	char *token;
	int rc;
	SERVICE_STATUS service_status;

	service_handle = RegisterServiceCtrlHandler("mosquitto", ServiceHandler);
	if(service_handle){
		argv = _mosquitto_malloc(sizeof(char *)*3);
		argv[0] = "mosquitto";
		argv[1] = "-c";
		argv[2] = NULL; //FIXME path to mosquitto.conf - get from env var
		argc = 3;

		service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
		service_status.dwCurrentState = SERVICE_RUNNING;
		service_status.dwControlsAccepted = SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_STOP;
		service_status.dwWin32ExitCode = NO_ERROR;
		service_status.dwCheckPoint = 0;
		SetServiceStatus(service_handle, &service_status);

		main(argc, argv);
		_mosquitto_free(argv);

		service_status.dwCurrentState = SERVICE_STOPPED;
		SetServiceStatus(service_handle, &service_status);
	}
}

#endif
