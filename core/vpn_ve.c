/*
 * Copyright (c) 2007, 2008 University of Tsukuba
 * Copyright (C) 2007, 2008
 *      National Institute of Information and Communications Technology
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the University of Tsukuba nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef CRYPTO_VPN
#ifdef VPN_VE
#include "config.h"
#include "cpu_mmu.h"
#include "crypt.h"
#include "current.h"
#include "initfunc.h"
#include "printf.h"
#include "spinlock.h"
#include "string.h"
#include "vmmcall.h"
#include "vpn_ve.h"
#include "vpnsys.h"

// NIC
struct VPN_NIC
{
	VPN_CTX *VpnCtx;					// ����ƥ�����
	SE_NICINFO NicInfo;					// NIC ����
	SE_SYS_CALLBACK_RECV_NIC *RecvCallback;	// �ѥ��åȼ������Υ�����Хå�
	void *RecvCallbackParam;			// ������Хå��ѥ�᡼��
	SE_QUEUE *SendPacketQueue;			// �����ѥ��åȥ��塼
	SE_QUEUE *RecvPacketQueue;			// �����ѥ��åȥ��塼
	bool IsVirtual;						// ���� NIC ���ɤ���
	SE_LOCK *Lock;						// ��å�
};
// VPN ���饤����ȥ���ƥ�����
struct VPN_CTX
{
	SE_HANDLE VpnClientHandle;			// VPN ���饤����ȥϥ�ɥ�
	VPN_NIC *PhysicalNic;				// ʪ�� NIC
	SE_HANDLE PhysicalNicHandle;		// ʪ�� NIC �ϥ�ɥ�
	VPN_NIC *VirtualNic;				// ���� NIC
	SE_HANDLE VirtualNicHandle;			// ���� NIC �ϥ�ɥ�
	SE_LOCK *LogQueueLock;				// ���Υ��塼�Υ�å�
	SE_QUEUE *LogQueue;					// ���Υ��塼
};

void crypt_sys_log(char *type, char *message);
void crypt_sys_get_physical_nic_info(SE_HANDLE nic_handle, SE_NICINFO *info);
void crypt_sys_send_physical_nic(SE_HANDLE nic_handle, UINT num_packets, void **packets, UINT *packet_sizes);
void crypt_sys_set_physical_nic_recv_callback(SE_HANDLE nic_handle, SE_SYS_CALLBACK_RECV_NIC *callback, void *param);
void crypt_sys_get_virtual_nic_info(SE_HANDLE nic_handle, SE_NICINFO *info);
void crypt_sys_send_virtual_nic(SE_HANDLE nic_handle, UINT num_packets, void **packets, UINT *packet_sizes);
void crypt_sys_set_virtual_nic_recv_callback(SE_HANDLE nic_handle, SE_SYS_CALLBACK_RECV_NIC *callback, void *param);
void crypt_nic_recv_packet(VPN_NIC *n, UINT num_packets, void **packets, UINT *packet_sizes);
VPN_NIC *crypt_init_physical_nic(VPN_CTX *ctx);
VPN_NIC *crypt_init_virtual_nic(VPN_CTX *ctx);
void crypt_flush_old_packet_from_queue(SE_QUEUE *q);

static VPN_CTX *vpn_ctx = NULL;			// VPN ���饤����ȥ���ƥ�����

// ve (Virtual Ethernet)
static spinlock_t ve_lock;

// Virtual Ethernet (ve) �ᥤ�����
void crypt_ve_main(UCHAR *in, UCHAR *out)
{
	VE_CTL *cin, *cout;
	VPN_NIC *nic = NULL;
	// ���������å�
	if (in == NULL || out == NULL)
	{
		return;
	}

	cin = (VE_CTL *)in;
	cout = (VE_CTL *)out;

	memset(cout, 0, sizeof(VE_CTL));

	if (cin->EthernetType == VE_TYPE_PHYSICAL)
	{
		// ʪ��Ū�� LAN ������
		nic = vpn_ctx->PhysicalNic;
	}
	else if (cin->EthernetType == VE_TYPE_VIRTUAL)
	{
		// ����Ū�� LAN ������
		nic = vpn_ctx->VirtualNic;
	}

	if (nic != NULL)
	{
		if (cin->Operation == VE_OP_GET_LOG)
		{
			// ������ 1 �Լ���
			SeLock(vpn_ctx->LogQueueLock);
			{
				char *str = SeGetNext(vpn_ctx->LogQueue);

				if (str != NULL)
				{
					cout->PacketSize = SeStrSize(str);
					SeStrCpy((char *)cout->PacketData, sizeof(cout->PacketData), str);

					SeFree(str);
				}

				cout->NumQueue = vpn_ctx->LogQueue->num_item;

				cout->RetValue = 1;
			}
			SeUnlock(vpn_ctx->LogQueueLock);
		}
		else if (cin->Operation == VE_OP_GET_NEXT_SEND_PACKET)
		{
			// �����������٤��ѥ��åȤμ��� (vpn -> vmm -> guest)
			SeLock(nic->Lock);
			{
				// �Ť��ѥ��åȤ���������˴�
				crypt_flush_old_packet_from_queue(nic->SendPacketQueue);

				// �ѥ��åȤ� 1 �İʾ奭�塼�ˤ��뤫�ɤ���
				if (nic->SendPacketQueue->num_item >= 1)
				{
					// ���塼���鼡�Υѥ��åȤ�Ȥ�
					void *packet_data = SeGetNext(nic->SendPacketQueue);
					UINT packet_data_size = SeMemSize(packet_data);
					UINT packet_size_real = packet_data_size - sizeof(UINT64);
					void *packet_data_real = ((UCHAR *)packet_data) + sizeof(UINT64);

					memcpy(cout->PacketData, packet_data_real, packet_size_real);
					cout->PacketSize = packet_size_real;

					cout->NumQueue = nic->SendPacketQueue->num_item;

					// �������
					SeFree(packet_data);
				}

				cout->RetValue = 1;
			}
			SeUnlock(nic->Lock);
		}
		else if (cin->Operation == VE_OP_PUT_RECV_PACKET)
		{
			bool flush = false;
			UINT num_packets = 0;
			void **packets = NULL;
			UINT *packet_sizes = NULL;

			// ���������ѥ��åȤν񤭹��� (guest -> vmm -> vpn)
			SeLock(nic->Lock);
			{
				// �����ѥ��åȤϡ��ѥե����ޥ󥹸���Τ���
				// ������ vpn ���Ϥ����ˤ��ä���������塼�ˤ����
				void *packet_data;
				UINT packet_size = cin->PacketSize;

				if (packet_size >= 1)
				{
					packet_data = SeClone(cin->PacketData, packet_size);

					SeInsertQueue(nic->RecvPacketQueue, packet_data);
				}

				if (cin->NumQueue == 0)
				{
					// �⤦����ʾ�����ѥ��åȤ�̵������
					// flush ���� (vpn �˰쵤���Ϥ�)
					flush = true;
				}

				cout->RetValue = 1;

				if (flush)
				{
					UINT i;
					void *p;

					num_packets = nic->RecvPacketQueue->num_item;
					packets = SeMalloc(sizeof(void *) * num_packets);
					packet_sizes = SeMalloc(sizeof(UINT *) * num_packets);

					i = 0;

					while (true)
					{
						UINT size;
						p = SeGetNext(nic->RecvPacketQueue);
						if (p == NULL)
						{
							break;
						}

						size = SeMemSize(p);

						packets[i] = p;
						packet_sizes[i] = size;

						i++;
					}
				}
			}
			SeUnlock(nic->Lock);

			if (flush)
			{
				UINT i;

				crypt_nic_recv_packet(nic, num_packets, packets, packet_sizes);

				for (i = 0;i < num_packets;i++)
				{
					SeFree(packets[i]);
				}

				SeFree(packets);
				SeFree(packet_sizes);
			}
		}
	}
}

// Virtual Ethernet (ve) �ϥ�ɥ�
void crypt_ve_handler()
{
	UINT i;
	UCHAR data[VE_BUFSIZE];
	UCHAR data2[VE_BUFSIZE];
	intptr addr;
	bool ok = true;

	if (!config.vmm.driver.vpn.ve)
		return;
	spinlock_lock(&ve_lock);

	current->vmctl.read_general_reg(GENERAL_REG_RBX, &addr);

	for (i = 0;i < (VE_BUFSIZE / 4);i++)
	{
		if (read_linearaddr_l((u32)((ulong)(((UCHAR *)addr) + (i * 4))), (u32 *)(data + (i * 4))) != VMMERR_SUCCESS)
		{
			ok = false;
			break;
		}
	}
	
#ifndef	CRYPTO_VPN
	ok = false;
#endif	// CRYPTO_VPN

	if (ok == false)
	{
		current->vmctl.write_general_reg(GENERAL_REG_RAX, 0);

		spinlock_unlock(&ve_lock);
		return;
	}

	crypt_ve_main(data, data2);

	for (i = 0;i < (VE_BUFSIZE / 4);i++)
	{
		if (write_linearaddr_l((u32)((ulong)(((UCHAR *)addr) + (i * 4))), *((UINT *)(&data2[i * 4]))) != VMMERR_SUCCESS)
		{
			ok = false;
			break;
		}
	}

	if (ok == false)
	{
		current->vmctl.write_general_reg(GENERAL_REG_RAX, 0);

		spinlock_unlock(&ve_lock);
		return;
	}

	current->vmctl.write_general_reg(GENERAL_REG_RAX, 1);

	spinlock_unlock(&ve_lock);
}

// Virtual Ethernet (ve) �ν����
void crypt_ve_init()
{
	printf("Initing Virtual Ethernet (VE) for VPN Client Module...\n");
	spinlock_init(&ve_lock);
	vmmcall_register("ve", crypt_ve_handler);
	printf("Virtual Ethernet (VE): Ok.\n");
}

INITFUNC ("vmmcal9", crypt_ve_init);

// ʪ�� NIC �ν����
VPN_NIC *crypt_init_physical_nic(VPN_CTX *ctx)
{
	VPN_NIC *n = SeZeroMalloc(sizeof(VPN_NIC));

	SeStrToBinEx(n->NicInfo.MacAddress, sizeof(n->NicInfo.MacAddress),
		"00:12:12:12:12:12");
	n->NicInfo.MediaSpeed = 1000000000;
	n->NicInfo.MediaType = SE_MEDIA_TYPE_ETHERNET;
	n->NicInfo.Mtu = 1500;

	n->RecvPacketQueue = SeNewQueue();
	n->SendPacketQueue = SeNewQueue();

	n->VpnCtx = ctx;

	n->IsVirtual = false;

	n->Lock = SeNewLock();

	return n;
}

// ���� NIC �κ���
VPN_NIC *crypt_init_virtual_nic(VPN_CTX *ctx)
{
	VPN_NIC *n = SeZeroMalloc(sizeof(VPN_NIC));

	SeStrToBinEx(n->NicInfo.MacAddress, sizeof(n->NicInfo.MacAddress),
		"00:AC:AC:AC:AC:AC");
	n->NicInfo.MediaSpeed = 1000000000;
	n->NicInfo.MediaType = SE_MEDIA_TYPE_ETHERNET;
	n->NicInfo.Mtu = 1500;

	n->RecvPacketQueue = SeNewQueue();
	n->SendPacketQueue = SeNewQueue();

	n->VpnCtx = ctx;

	n->IsVirtual = true;

	n->Lock = SeNewLock();

	return n;
}

static struct nicfunc vefunc = {
	.GetPhysicalNicInfo = crypt_sys_get_physical_nic_info,
	.SendPhysicalNic = crypt_sys_send_physical_nic,
	.SetPhysicalNicRecvCallback = crypt_sys_set_physical_nic_recv_callback,
	.GetVirtualNicInfo = crypt_sys_get_virtual_nic_info,
	.SendVirtualNic = crypt_sys_send_virtual_nic,
	.SetVirtualNicRecvCallback = crypt_sys_set_virtual_nic_recv_callback,
};

// VPN ���饤����Ȥν����
void crypt_init_vpn()
{
	vpn_ctx = SeZeroMalloc(sizeof(VPN_CTX));

	// ���ѥ��塼�ν����
	vpn_ctx->LogQueue = SeNewQueue();
	vpn_ctx->LogQueueLock = SeNewLock();

	// ʪ�� NIC �κ���
	vpn_ctx->PhysicalNic = crypt_init_physical_nic(vpn_ctx);
	vpn_ctx->PhysicalNicHandle = (SE_HANDLE)vpn_ctx->PhysicalNic;

	// ���� NIC �κ���
	vpn_ctx->VirtualNic = crypt_init_virtual_nic(vpn_ctx);
	vpn_ctx->VirtualNicHandle = (SE_HANDLE)vpn_ctx->VirtualNic;

	// VPN Client �κ���
	//vpn_ctx->VpnClientHandle = VPN_IPsec_Client_Start(vpn_ctx->PhysicalNicHandle, vpn_ctx->VirtualNicHandle, "config.txt");
	vpn_ctx->VpnClientHandle = vpn_new_nic (vpn_ctx->PhysicalNicHandle,
						vpn_ctx->VirtualNicHandle,
						&vefunc);
}

// �󶡥����ƥॳ����: ���ν��� (���̤�ɽ��)
void crypt_sys_log(char *type, char *message)
{
	char *lf = "\n";
	void crypt_add_log_queue(char *type, char *message);

	if (message[strlen(message) - 1] == '\n')
	{
		lf = "";
	}

	if (type != NULL && SeStrLen(type) >= 1)
	{
		printf("%s: %s%s", type, message, lf);
	}
	else
	{
		printf("%s%s", message, lf);
	}

	crypt_add_log_queue(type, message);
}

// �����塼�˥��ǡ������ɲ�
void crypt_add_log_queue(char *type, char *message)
{
	char *tmp;
	char tmp2[512];
	// ���������å�
	if (type == NULL || message == NULL)
	{
		return;
	}
	if (vpn_ctx == NULL)
	{
		return;
	}

	tmp = SeCopyStr(message);
	if (SeStrLen(tmp) >= 1)
	{
		if (tmp[SeStrLen(tmp) - 1] == '\n')
		{
			tmp[SeStrLen(tmp) - 1] = 0;
		}
	}
	if (SeStrLen(tmp) >= 1)
	{
		if (tmp[SeStrLen(tmp) - 1] == '\r')
		{
			tmp[SeStrLen(tmp) - 1] = 0;
		}
	}

	if (type != NULL && SeStrLen(type) >= 1)
	{
		SeFormat(tmp2, sizeof(tmp2), "%s: %s", type, tmp);
	}
	else
	{
		SeStrCpy(tmp2, sizeof(tmp2), tmp);
	}

	SeFree(tmp);

	SeLock(vpn_ctx->LogQueueLock);
	{
		while (vpn_ctx->LogQueue->num_item > CRYPT_MAX_LOG_QUEUE_LINES)
		{
			char *p = SeGetNext(vpn_ctx->LogQueue);

			SeFree(p);
		}

		SeInsertQueue(vpn_ctx->LogQueue, SeCopyStr(tmp2));
	}
	SeUnlock(vpn_ctx->LogQueueLock);
}

// �󶡥����ƥॳ����: ʪ�� NIC �ξ���μ���
void crypt_sys_get_physical_nic_info(SE_HANDLE nic_handle, SE_NICINFO *info)
{
	VPN_NIC	*n = (VPN_NIC *)nic_handle;
	// ���������å�
	if (n == NULL)
	{
		return;
	}

	SeCopy(info, &n->NicInfo, sizeof(SE_NICINFO));
}

// �����ѥ��åȥ��塼����Ť��ѥ��åȤ�������
void crypt_flush_old_packet_from_queue(SE_QUEUE *q)
{
	UINT64 now = SeTick64();
	UINT num = 0;

	while (true)
	{
		void *data = SePeekNext(q);
		UINT64 *time_stamp;

		if (data == NULL)
		{
			break;
		}

		time_stamp = (UINT64 *)data;

		if (now <= ((*time_stamp) + CRYPT_SEND_PACKET_LIFETIME))
		{
			break;
		}

		data = SeGetNext(q);

		SeFree(data);

		num++;
	}
}

// �󶡥����ƥॳ����: ʪ�� NIC ���Ѥ��ƥѥ��åȤ�����
void crypt_sys_send_physical_nic(SE_HANDLE nic_handle, UINT num_packets, void **packets, UINT *packet_sizes)
{
	VPN_NIC	*n = (VPN_NIC *)nic_handle;
	UINT i;
	// ���������å�
	if (n == NULL || num_packets == 0 || packets == NULL || packet_sizes == NULL)
	{
		return;
	}

	SeLock(n->Lock);
	{
		// �ѥ��åȤ򥭥塼�˳�Ǽ����
		for (i = 0;i < num_packets;i++)
		{
			void *packet = packets[i];
			UINT size = packet_sizes[i];

			UCHAR *packet_copy = SeMalloc(size + sizeof(UINT64));
			SeCopy(packet_copy + sizeof(UINT64), packet, size);
			*((UINT64 *)packet_copy) = SeTick64();

			SeInsertQueue(n->SendPacketQueue, packet_copy);
		}

		// �Ť��ѥ��åȤ��������塼����������
		crypt_flush_old_packet_from_queue(n->SendPacketQueue);
	}
	SeUnlock(n->Lock);
}

// �󶡥����ƥॳ����: ʪ�� NIC ����ѥ��åȤ���������ݤΥ�����Хå�������
void crypt_sys_set_physical_nic_recv_callback(SE_HANDLE nic_handle, SE_SYS_CALLBACK_RECV_NIC *callback, void *param)
{
	VPN_NIC	*n = (VPN_NIC *)nic_handle;
	// ���������å�
	if (n == NULL)
	{
		return;
	}

	n->RecvCallback = callback;
	n->RecvCallbackParam = param;
}

// �󶡥����ƥॳ����: ���� NIC �ξ���μ���
void crypt_sys_get_virtual_nic_info(SE_HANDLE nic_handle, SE_NICINFO *info)
{
	VPN_NIC	*n = (VPN_NIC *)nic_handle;

	SeCopy(info, &n->NicInfo, sizeof(SE_NICINFO));
}

// �󶡥����ƥॳ����: ���� NIC ���Ѥ��ƥѥ��åȤ�����
void crypt_sys_send_virtual_nic(SE_HANDLE nic_handle, UINT num_packets, void **packets, UINT *packet_sizes)
{
	VPN_NIC	*n = (VPN_NIC *)nic_handle;
	UINT i;
	// ���������å�
	if (n == NULL || num_packets == 0 || packets == NULL || packet_sizes == NULL)
	{
		return;
	}

	SeLock(n->Lock);
	{
		// �ѥ��åȤ򥭥塼�˳�Ǽ����
		for (i = 0;i < num_packets;i++)
		{
			void *packet = packets[i];
			UINT size = packet_sizes[i];

			UCHAR *packet_copy = SeMalloc(size + sizeof(UINT64));
			SeCopy(packet_copy + sizeof(UINT64), packet, size);
			*((UINT64 *)packet_copy) = SeTick64();

			SeInsertQueue(n->SendPacketQueue, packet_copy);
		}

		// �Ť��ѥ��åȤ��������塼����������
		crypt_flush_old_packet_from_queue(n->SendPacketQueue);
	}
	SeUnlock(n->Lock);
}

// �󶡥����ƥॳ����: ���� NIC ����ѥ��åȤ���������ݤΥ�����Хå�������
void crypt_sys_set_virtual_nic_recv_callback(SE_HANDLE nic_handle, SE_SYS_CALLBACK_RECV_NIC *callback, void *param)
{
	VPN_NIC	*n = (VPN_NIC *)nic_handle;
	// ���������å�
	if (n == NULL)
	{
		return;
	}

	n->RecvCallback = callback;
	n->RecvCallbackParam = param;
}

// ʪ�� / ���� NIC �ǥѥ��åȤ�����������Ȥ����Τ��ѥ��åȥǡ������Ϥ�
void crypt_nic_recv_packet(VPN_NIC *n, UINT num_packets, void **packets, UINT *packet_sizes)
{
	// ���������å�
	if (n == NULL || num_packets == 0 || packets == NULL || packet_sizes == NULL)
	{
		return;
	}

	n->RecvCallback((SE_HANDLE)n, num_packets, packets, packet_sizes, n->RecvCallbackParam);
}

static void
vpn_ve_init (void)
{
	if (!config.vmm.driver.vpn.ve)
		return;
	crypt_init_vpn ();
}

INITFUNC ("driver1", vpn_ve_init);
#endif /* VPN_VE */
#endif /* CRYPTO_VPN */
