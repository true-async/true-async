/* Which partial reads queue a completion packet, and what GQCS reports for it.
 * Build: cl /nologo moredata.c ws2_32.lib   Run: moredata.exe */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>

static HANDLE port;

static void drain(const char *what, OVERLAPPED *expected)
{
	for (;;) {
		DWORD bytes = 0;
		ULONG_PTR key = 0;
		OVERLAPPED *ov = NULL;
		const BOOL ok = GetQueuedCompletionStatus(port, &bytes, &key, &ov, 500);
		const DWORD err = ok ? 0 : GetLastError();
		if (!ok && ov == NULL) {
			printf("  %s: no more packets (GQCS err %lu)\n", what, err);
			return;
		}
		printf("  %s: packet ok=%d err=%lu bytes=%lu key=%llu ours=%d Internal=0x%llx InternalHigh=%llu\n",
				what, ok, err, bytes, (unsigned long long) key, ov == expected,
				(unsigned long long) ov->Internal, (unsigned long long) ov->InternalHigh);
	}
}

static void message_pipe(int pending)
{
	static int n = 0;
	char name[64];
	snprintf(name, sizeof(name), "\\\\.\\pipe\\moredata-%lu-%d", GetCurrentProcessId(), n++);
	HANDLE server = CreateNamedPipeA(name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
			PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 4096, 4096, 0, NULL);
	HANDLE client = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	CreateIoCompletionPort(server, port, 11, 0);
	DWORD got = 0;
	if (!pending) {
		WriteFile(client, "0123456789", 10, &got, NULL);
	}

	OVERLAPPED ov = { 0 };
	char buf[4] = { 0 };
	const BOOL ok = ReadFile(server, buf, sizeof(buf), NULL, &ov);
	const DWORD err = ok ? 0 : GetLastError();
	printf("message pipe, %s: ReadFile ok=%d err=%lu Internal=0x%llx InternalHigh=%llu\n",
			pending ? "pending" : "data queued", ok, err, (unsigned long long) ov.Internal,
			(unsigned long long) ov.InternalHigh);
	if (pending) {
		WriteFile(client, "0123456789", 10, &got, NULL);
	}

	drain("message pipe", &ov);
	DWORD bytes = 0;
	const BOOL r = GetOverlappedResult(server, &ov, &bytes, FALSE);
	printf("  GetOverlappedResult ok=%d err=%lu bytes=%lu buf=%.4s\n", r, r ? 0 : GetLastError(), bytes, buf);
	CloseHandle(client);
	CloseHandle(server);
}

static void udp(int pending)
{
	SOCKET rx = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_OVERLAPPED);
	SOCKET tx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	struct sockaddr_in a = { 0 };
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	bind(rx, (struct sockaddr *) &a, sizeof(a));
	int alen = sizeof(a);
	getsockname(rx, (struct sockaddr *) &a, &alen);
	CreateIoCompletionPort((HANDLE) rx, port, 22, 0);
	if (!pending) {
		sendto(tx, "0123456789", 10, 0, (struct sockaddr *) &a, sizeof(a));
		Sleep(50);
	}

	WSAOVERLAPPED ov = { 0 };
	char buf[4] = { 0 };
	WSABUF wb = { sizeof(buf), buf };
	DWORD flags = 0;
	const int rc = WSARecv(rx, &wb, 1, NULL, &flags, &ov, NULL);
	const int err = rc == 0 ? 0 : WSAGetLastError();
	printf("udp, %s: WSARecv rc=%d err=%d Internal=0x%llx InternalHigh=%llu\n",
			pending ? "pending" : "datagram queued", rc, err, (unsigned long long) ov.Internal,
			(unsigned long long) ov.InternalHigh);
	if (pending) {
		sendto(tx, "0123456789", 10, 0, (struct sockaddr *) &a, sizeof(a));
	}

	drain("udp", &ov);
	DWORD bytes = 0;
	DWORD f2 = 0;
	const BOOL r = WSAGetOverlappedResult(rx, &ov, &bytes, FALSE, &f2);
	printf("  WSAGetOverlappedResult ok=%d err=%d bytes=%lu flags=0x%lx buf=%.4s\n", r,
			r ? 0 : WSAGetLastError(), bytes, f2, buf);
	closesocket(tx);
	closesocket(rx);
}

static void file_eof(void)
{
	char path[MAX_PATH];
	GetTempPathA(sizeof(path), path);
	strcat(path, "moredata-eof.txt");
	HANDLE w = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
	DWORD got = 0;
	WriteFile(w, "abc", 3, &got, NULL);
	CloseHandle(w);
	HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	CreateIoCompletionPort(f, port, 33, 0);
	OVERLAPPED ov = { 0 };
	ov.Offset = 100;
	char buf[4];
	const BOOL ok = ReadFile(f, buf, sizeof(buf), NULL, &ov);
	printf("file read past EOF: ReadFile ok=%d err=%lu Internal=0x%llx\n", ok, ok ? 0 : GetLastError(),
			(unsigned long long) ov.Internal);
	drain("file eof", &ov);
	CloseHandle(f);
	DeleteFileA(path);
}

int main(void)
{
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
	port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	message_pipe(0);
	message_pipe(1);
	udp(0);
	udp(1);
	file_eof();
	return 0;
}
