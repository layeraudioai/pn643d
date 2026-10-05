#include "DownloadPlayHost.h"

#include <3ds.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#if defined(DAEDALUS_DOWNLOADPLAY_HOST)

#ifndef DAEDALUS_DOWNLOADPLAY_CHILD_INDEX
#error "DAEDALUS_DOWNLOADPLAY_CHILD_INDEX must match the DlpChild title UniqueId low byte"
#endif

namespace
{
	// The documented Initialize request specifies two 0x40000 transfer blocks.
	// The allocation size/permissions and event behavior are inferred and must
	// be verified on hardware before this can be treated as production-ready.
	static const u32 kTransferBlockSize = 0x40000;
	static const u32 kTransferBlockCount = 2;
	static const u32 kSharedMemorySize = kTransferBlockSize * kTransferBlockCount;
	static const u32 kMaxClients = 4;
	static const u8 kWirelessChannel = 1;
	static const size_t kMaxTrackedClients = 4;

	static Handle s_service = 0;
	static Handle s_sharedMemory = 0;
	static Handle s_event = 0;
	static void *s_sharedMemoryBacking = NULL;
	static bool s_initialized = false;
	static bool s_startedAccepting = false;
	static CTRDownloadPlayHost::State s_state = CTRDownloadPlayHost::STATE_OFF;
	static u16 s_nodeIds[kMaxTrackedClients] = {};
	static unsigned s_clientCount = 0;
	static unsigned s_progressPercent = 0;
	static bool s_allTransfersComplete = false;
	static char s_status[128] = "Download Play host is idle.";
	static char s_passphrase[10] = {};

	static void SetStatus(const char *text)
	{
		snprintf(s_status, sizeof(s_status), "%s", text ? text : "");
	}

	static void SetResultStatus(const char *operation, Result result)
	{
		snprintf(s_status, sizeof(s_status), "%s failed (0x%08lX).",
			operation, static_cast<unsigned long>(result));
		s_state = CTRDownloadPlayHost::STATE_ERROR;
	}

	static Result SimpleRequest(u16 commandId)
	{
		u32 *cmdbuf = getThreadCommandBuffer();
		cmdbuf[0] = IPC_MakeHeader(commandId, 0, 0);
		Result result = svcSendSyncRequest(s_service);
		if (R_FAILED(result)) return result;
		return static_cast<Result>(cmdbuf[1]);
	}

	static bool HasNode(u16 nodeId)
	{
		for (unsigned i = 0; i < s_clientCount; ++i)
			if (s_nodeIds[i] == nodeId) return true;
		return false;
	}

	static Result GetConnectingClients(u16 *nodes, unsigned capacity, unsigned *count)
	{
		u32 *cmdbuf = getThreadCommandBuffer();
		memset(nodes, 0, capacity * sizeof(*nodes));
		cmdbuf[0] = IPC_MakeHeader(0x000B, 1, 2);
		cmdbuf[1] = capacity;
		cmdbuf[2] = IPC_Desc_Buffer(capacity * sizeof(*nodes), IPC_BUFFER_W);
		cmdbuf[3] = static_cast<u32>(reinterpret_cast<uintptr_t>(nodes));
		Result result = svcSendSyncRequest(s_service);
		if (R_FAILED(result)) return result;
		result = static_cast<Result>(cmdbuf[1]);
		if (R_FAILED(result)) return result;
		*count = cmdbuf[2] > capacity ? capacity : cmdbuf[2];
		return 0;
	}

	static Result AcceptClient(u16 nodeId)
	{
		u32 *cmdbuf = getThreadCommandBuffer();
		cmdbuf[0] = IPC_MakeHeader(0x0009, 1, 0);
		cmdbuf[1] = nodeId;
		Result result = svcSendSyncRequest(s_service);
		if (R_FAILED(result)) return result;
		return static_cast<Result>(cmdbuf[1]);
	}

	static Result GetClientStatus(u16 nodeId, u32 *status0, u32 *totalUnits, u32 *sentUnits)
	{
		u32 *cmdbuf = getThreadCommandBuffer();
		cmdbuf[0] = IPC_MakeHeader(0x000D, 1, 0);
		cmdbuf[1] = nodeId;
		Result result = svcSendSyncRequest(s_service);
		if (R_FAILED(result)) return result;
		result = static_cast<Result>(cmdbuf[1]);
		if (R_FAILED(result)) return result;
		*status0 = cmdbuf[2];
		*totalUnits = cmdbuf[3];
		*sentUnits = cmdbuf[4];
		return 0;
	}

	static Result PollDistribution()
	{
		if (!s_clientCount) return 0;
		unsigned complete = 0;
		unsigned long long sentTotal = 0;
		unsigned long long unitsTotal = 0;
		for (unsigned i = 0; i < s_clientCount; ++i)
		{
			u32 status0 = 0, total = 0, sent = 0;
			Result result = GetClientStatus(s_nodeIds[i], &status0, &total, &sent);
			if (R_FAILED(result)) return result;
			(void)status0; // Client state enum is not specified in the public notes.
			if (total && sent >= total) ++complete;
			sentTotal += sent;
			unitsTotal += total;
		}
		s_progressPercent = unitsTotal
			? static_cast<unsigned>((sentTotal * 100) / unitsTotal) : 0;
		s_allTransfersComplete = complete == s_clientCount;
		return 0;
	}

	static Result InitializeService()
	{
		Result result = srvGetServiceHandle(&s_service, "dlp:SRVR");
		if (R_FAILED(result)) return result;

		s_sharedMemoryBacking = linearAlloc(kSharedMemorySize);
		if (!s_sharedMemoryBacking)
			return static_cast<Result>(0xD9001830); // local out-of-memory sentinel, not a service result
		memset(s_sharedMemoryBacking, 0, kSharedMemorySize);

		result = svcCreateMemoryBlock(&s_sharedMemory,
			static_cast<u32>(reinterpret_cast<uintptr_t>(s_sharedMemoryBacking)),
			kSharedMemorySize,
			static_cast<MemPerm>(MEMPERM_READ | MEMPERM_WRITE),
			static_cast<MemPerm>(MEMPERM_READ | MEMPERM_WRITE));
		if (R_FAILED(result)) return result;
		result = svcCreateEvent(&s_event, RESET_ONESHOT);
		if (R_FAILED(result)) return result;

		u32 processId = 0;
		result = svcGetProcessId(&processId, CUR_PROCESS_HANDLE);
		if (R_FAILED(result)) return result;

		u32 *cmdbuf = getThreadCommandBuffer();
		cmdbuf[0] = IPC_MakeHeader(0x0001, 6, 3);
		cmdbuf[1] = kSharedMemorySize;
		cmdbuf[2] = kMaxClients;
		cmdbuf[3] = processId;
		cmdbuf[4] = DAEDALUS_DOWNLOADPLAY_CHILD_INDEX;
		cmdbuf[5] = kTransferBlockSize;
		cmdbuf[6] = kTransferBlockCount;
		cmdbuf[7] = IPC_Desc_MoveHandles(2);
		cmdbuf[8] = s_sharedMemory;
		cmdbuf[9] = s_event;
		result = svcSendSyncRequest(s_service);
		if (R_FAILED(result)) return result;
		result = static_cast<Result>(cmdbuf[1]);
		if (R_FAILED(result)) return result;

		// Initialize's public IPC description calls for two transferred handles.
		// The exact ownership/lifetime contract is unverified; do not close these
		// here after a successful move. Finalize tears down service-owned state.
		s_sharedMemory = 0;
		s_event = 0;
		s_initialized = true;
		return 0;
	}

	static Result StartAccepting()
	{
		u32 *cmdbuf = getThreadCommandBuffer();
		cmdbuf[0] = IPC_MakeHeader(0x0005, 2, 0);
		cmdbuf[1] = 1; // manual acceptance: this app accepts each advertised node
		cmdbuf[2] = kWirelessChannel;
		Result result = svcSendSyncRequest(s_service);
		if (R_FAILED(result)) return result;
		return static_cast<Result>(cmdbuf[1]);
	}

	static Result SendPassphrase()
	{
		// The DLP protocol allows a 9-byte passphrase. Use eight ASCII hex bytes
		// plus NUL; clients later obtain it through the system Download Play app.
		const u32 seed = static_cast<u32>(osGetTime()) ^ static_cast<u32>(svcGetSystemTick());
		snprintf(s_passphrase, sizeof(s_passphrase), "%08lX", static_cast<unsigned long>(seed));
		u32 *cmdbuf = getThreadCommandBuffer();
		cmdbuf[0] = IPC_MakeHeader(0x0008, 3, 0);
		memcpy(&cmdbuf[1], s_passphrase, 8);
		cmdbuf[3] = 0;
		Result result = svcSendSyncRequest(s_service);
		if (R_FAILED(result)) return result;
		return static_cast<Result>(cmdbuf[1]);
	}
}

namespace CTRDownloadPlayHost
{
	bool Start()
	{
		if (s_state == STATE_ACCEPTING || s_state == STATE_DISTRIBUTING)
			return true;
		Stop();

		// Do not begin a session unless romconvert bundled the actual child CIA.
		// DLP SRVR's published StartDistribution command has no file/path input;
		// whether the system module resolves the child CIA from the host RomFS is
		// unverified and is a hardware validation requirement.
		FILE *cia = fopen("romfs:/downloadplay-child.cia", "rb");
		if (!cia)
		{
			SetStatus("Bundled child CIA not found in RomFS.");
			s_state = STATE_ERROR;
			return false;
		}
		if (fseek(cia, 0, SEEK_END) != 0)
		{
			fclose(cia);
			SetStatus("Could not inspect bundled child CIA.");
			s_state = STATE_ERROR;
			return false;
		}
		const long ciaSize = ftell(cia);
		fclose(cia);
		if (ciaSize <= 0 || static_cast<unsigned long>(ciaSize) > 32ul * 1024ul * 1024ul)
		{
			SetStatus("Bundled child CIA is empty or exceeds 32 MiB.");
			s_state = STATE_ERROR;
			return false;
		}

		Result result = InitializeService();
		if (R_FAILED(result))
		{
			Stop();
			SetResultStatus("DLP server initialization / IPC setup", result);
			return false;
		}
		result = StartAccepting();
		if (R_FAILED(result))
		{
			Stop();
			SetResultStatus("StartAccepting", result);
			return false;
		}
		s_startedAccepting = true;
		s_state = STATE_ACCEPTING;
		s_clientCount = 0;
		s_progressPercent = 0;
		s_allTransfersComplete = false;
		SetStatus("Download Play session open; wait for clients to join.");
		return true;
	}

	void Tick()
	{
		if (s_state == STATE_ACCEPTING)
		{
			u16 nodes[kMaxTrackedClients] = {};
			unsigned count = 0;
			Result result = GetConnectingClients(nodes, kMaxTrackedClients, &count);
			if (R_FAILED(result))
			{
				SetResultStatus("GetConnectingClients", result);
				return;
			}
			for (unsigned i = 0; i < count && s_clientCount < kMaxTrackedClients; ++i)
			{
				if (!nodes[i] || HasNode(nodes[i])) continue;
				result = AcceptClient(nodes[i]);
				if (R_FAILED(result))
				{
					SetResultStatus("AcceptClient", result);
					return;
				}
				s_nodeIds[s_clientCount++] = nodes[i];
			}
			if (s_clientCount)
				snprintf(s_status, sizeof(s_status), "%u client(s) accepted; start distribution when ready.", s_clientCount);
		}
		else if (s_state == STATE_DISTRIBUTING)
		{
			Result result = PollDistribution();
			if (R_FAILED(result))
			{
				SetResultStatus("GetClientState", result);
				return;
			}
			if (s_allTransfersComplete)
			{
				s_state = STATE_FINISHED;
				SetStatus("Service reports transfer complete; verify client launch on hardware.");
			}
			else
				snprintf(s_status, sizeof(s_status), "Sending to %u client(s): %u%% (service progress; unverified).",
					s_clientCount, s_progressPercent);
		}
	}

	bool BeginDistribution()
	{
		if (s_state != STATE_ACCEPTING || s_clientCount == 0)
		{
			SetStatus("At least one Download Play client must join first.");
			return false;
		}
		Result result = SimpleRequest(0x0006); // EndAccepting
		if (R_FAILED(result))
		{
			SetResultStatus("EndAccepting", result);
			return false;
		}
		s_startedAccepting = false;
		result = SendPassphrase();
		if (R_FAILED(result))
		{
			SetResultStatus("SendWirelessRebootPassphrase", result);
			return false;
		}
		result = SimpleRequest(0x0007); // StartDistribution
		if (R_FAILED(result))
		{
			SetResultStatus("StartDistribution", result);
			return false;
		}
		s_state = STATE_DISTRIBUTING;
		s_progressPercent = 0;
		s_allTransfersComplete = false;
		SetStatus("Distribution started; waiting for service progress.");
		return true;
	}

	void Stop()
	{
		if (s_service)
		{
			if (s_startedAccepting)
				(void)SimpleRequest(0x0006); // EndAccepting
			if (s_initialized)
				(void)SimpleRequest(0x0002); // Finalize
			svcCloseHandle(s_service);
		}
		if (s_sharedMemory) svcCloseHandle(s_sharedMemory);
		if (s_event) svcCloseHandle(s_event);
		if (s_sharedMemoryBacking) linearFree(s_sharedMemoryBacking);
		s_service = 0;
		s_sharedMemory = 0;
		s_event = 0;
		s_sharedMemoryBacking = NULL;
		s_initialized = false;
		s_startedAccepting = false;
		s_clientCount = 0;
		s_progressPercent = 0;
		s_allTransfersComplete = false;
		memset(s_nodeIds, 0, sizeof(s_nodeIds));
		s_state = STATE_OFF;
		SetStatus("Download Play host is idle.");
	}

	State GetState() { return s_state; }
	unsigned GetClientCount() { return s_clientCount; }
	unsigned GetProgressPercent() { return s_progressPercent; }
	const char *GetStatus() { return s_status; }
}

#else
namespace CTRDownloadPlayHost
{
	bool Start() { return false; }
	bool BeginDistribution() { return false; }
	void Tick() {}
	void Stop() {}
	State GetState() { return STATE_UNAVAILABLE; }
	unsigned GetClientCount() { return 0; }
	unsigned GetProgressPercent() { return 0; }
	const char *GetStatus() { return "Build without a bundled Download Play child CIA."; }
}
#endif
