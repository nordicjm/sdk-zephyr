/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <ctype.h>
#include <zephyr/drivers/gpio.h>
#include <stdio.h>
#include <app_event_manager.h>
#include <date_time.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_smp_client, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <dfu/dfu_target_smp.h>
#include <net/fota_download.h>
#include <zephyr/shell/shell.h>

static void start_fota_download(struct k_work *work);

static K_SEM_DEFINE(state_mutex, 0, 1);
static K_WORK_DEFINE(download_work, start_fota_download);
static char fota_path[128];
static char fota_host[128];

static int hash_to_string(char *hash_string, size_t string_size,  uint8_t *hash)
{
        char *ptr = hash_string;
        int buf_size = string_size;
        int len = 0;

        for(int i=0; i< 32; i++) {
                len += snprintk(ptr + len , buf_size - len, "%x", hash[i]);
                if (len >= string_size) {
                        return -1;
                }
        }
        hash_string[len] = 0;

        return 0;
}

static void print_image_list(const struct shell *sh, struct mcumgr_image_state *image_list)
{
        struct mcumgr_image_data *list;
        char hash_string[(IMG_MGMT_HASH_LEN*2) + 1];

        list = image_list->image_list;
	for (int i = 0; i < image_list->image_list_length; i++) {
		if (list->flags.active) {
			shell_print(sh, "Primary Image(%d) slot(%d)", list->img_num, list->slot_num);
		} else {
			shell_print(sh, "Secondary Image(%d) slot(%d)", list->img_num, list->slot_num);
		}

		shell_print(sh, "       Version: %s", list->version);
                shell_print(sh, "       Bootable(%d) Pending(%d) Confirmed(%d)", list->flags.bootable,
			list->flags.pending, list->flags.confirmed);
		if (hash_to_string(hash_string, sizeof(hash_string), list->hash) == 0) {
			shell_print(sh, "       Hash: %s", hash_string);
		}

		list++;
	}
}

static void fota_download_callback(const struct fota_download_evt *evt)
{
	switch (evt->id) {
	/* These two cases return immediately */
	case FOTA_DOWNLOAD_EVT_PROGRESS:
		return;
	default:
		return;

	/* Following cases mark end of FOTA download */
	case FOTA_DOWNLOAD_EVT_CANCELLED:
		LOG_ERR("FOTA_DOWNLOAD_EVT_CANCELLED");
		break;
	case FOTA_DOWNLOAD_EVT_ERROR:
		LOG_ERR("FOTA_DOWNLOAD_EVT_ERROR: %d", evt->cause);
		break;

	case FOTA_DOWNLOAD_EVT_FINISHED:
		LOG_INF("FOTA download finished");
		break;
	}
}

static void start_fota_download(struct k_work *work)
{
	int ret;

	ret = fota_download_start_with_image_type(fota_host, fota_path, -1, 0, 0,
						  DFU_TARGET_IMAGE_TYPE_SMP);
	if (ret) {
		LOG_ERR("fota_download_start() failed, return code %d", ret);
	}

	return;
}

static void dfu_target_cb(enum dfu_target_evt_id evt)
{
	ARG_UNUSED(evt);
}

static int fota_start(void)
{
	int ret;

        /* Register Callback */
        ret = fota_download_smp_init(fota_download_callback);

	if (ret != 0) {
		LOG_ERR("fota_download_init() returned %d", ret);
		return -EBUSY;
	}


        /* Trigger download start */
	k_work_submit(&download_work);

	return 0;
}

static int fota_update(const struct shell *sh)
{
        int ret;
        struct mcumgr_image_state image_list;

        /* Init DFU target */
        ret = dfu_target_init(DFU_TARGET_IMAGE_TYPE_SMP, 0, 2048, dfu_target_cb);
        if (ret != 0) {
		LOG_ERR("dfu_target_init() returned %d", ret);
		return -EBUSY;
	}

	ret = dfu_target_schedule_update(1);
	if (ret != 0) {
		LOG_ERR("dfu_target_schedule_update() returned %d", ret);
		return -EBUSY;
	}

        ret =  dfu_target_smp_image_list_get(&image_list);

	if (ret) {

		return ret;
	}
        print_image_list(sh, &image_list);

	return ret;
}

static int fota_erase(void)
{
        int ret;

	/* Init DFU target */
	ret = dfu_target_init(DFU_TARGET_IMAGE_TYPE_SMP, 0, 2048, dfu_target_cb);
	if (ret != 0) {
		LOG_ERR("dfu_target_init() returned %d", ret);
		return -EBUSY;
	}

	ret = dfu_target_done(false);
	if (ret != 0) {
		LOG_ERR("dfu_target_done() returned %d", ret);
		return -EBUSY;
	}

        return ret;
}

static int nrf52840_reset_api(void)
{
	LOG_ERR("Reset not okay");
	return 0;
}

int main(void)
{
	int ret;

        /* DFU SMP Target init and register reset for recovery mode */
	ret = dfu_target_smp_client_init();
	if (ret) {
		LOG_ERR("Failed to init DFU target SMP, %d", ret);
		return ret;
	}
	dfu_target_smp_recovery_mode_enable(nrf52840_reset_api);

	ret = app_event_manager_init();
	if (ret) {
		LOG_ERR("Unable to init Application Event Manager (%d)", ret);
		return 0;
	}

	return 0;
}

static int cmd_download(const struct shell *sh, size_t argc, char **argv)
{
        int ret, len;
        char *uri, *e, *s;

	if (argc < 1) {
		shell_error(sh, "no arguments or path(s)\n");
		shell_help(sh);
		return -EINVAL;
        }

	uri = argv[1];

        LOG_INF("Download url %s", uri);

        /* Find the end of protocol marker https:// or coap:// */
	s = strstr(uri, "://");

	if (!s) {
		LOG_ERR("Host not found");
		return -EINVAL;
	}
	s += strlen("://");

	/* Find the end of host name, which is start of path */
	e = strchr(s, '/');

	if (!e) {
		LOG_ERR("Path not found");
		return -EINVAL;
	}

	/* Path can point to a string */
        strcpy(fota_path, e + 1);
	len = e - uri;

        if (len == sizeof(fota_host)) {
                LOG_ERR("Host Name too big %d", len);
		return -ENOMEM;
        }

	strncpy(fota_host, uri, len);
	fota_host[len] = 0;

        LOG_INF("Download Path %s host %s", fota_path, fota_host);
        ret = fota_start();

	if (ret < 0) {
		shell_error(
			sh,
			"can't do write operation, request failed (err %d)\n",
			ret);
		return -ENOEXEC;
	}

	return 0;
}

static int cmd_update(const struct shell *sh, size_t argc, char **argv)
{
        int ret;

        ret = fota_update(sh);

	if (ret < 0) {
		shell_error(
			sh,
			"can't do Update operation, request failed (err %d)\n",
			ret);
		return -ENOEXEC;
	}

        shell_print(sh, "Update OK");

	return 0;
}

static int cmd_erase(const struct shell *sh, size_t argc, char **argv)
{
        int ret;

        ret = fota_erase();

	if (ret < 0) {
		shell_error(
			sh,
			"can't do Erase operation, request failed (err %d)\n",
			ret);
		return -ENOEXEC;
	}

        shell_print(sh, "Erase OK");

	return 0;
}


static int cmd_reset(const struct shell *sh, size_t argc, char **argv)
{
        int ret;

        ret = dfu_target_reset();
	if (ret != 0) {
		ret = nrf52840_reset_api();
	}

	if (ret < 0) {
		shell_error(
			sh,
			"can't do RESET operation, request failed (err %d)\n",
			ret);
		return -ENOEXEC;
	}

        shell_print(sh, "Reset OK");

	return 0;
}

static int cmd_read_image_list(const struct shell *sh, size_t argc, char **argv)
{
        int ret;
        struct mcumgr_image_state image_list;


        ret =  dfu_target_smp_image_list_get(&image_list);

	if (ret < 0) {
		shell_error(
			sh,
			"can't do RESET operation, request failed (err %d)\n",
			ret);
		return -ENOEXEC;
	}
        print_image_list(sh, &image_list);

	return 0;
}

#define LWM2M_HELP_CMD "MCUmgr client commands"
#define LWM2M_HELP_DOWNLOAD "Start download/upload image from PATH\n"
#define LWM2M_HELP_ERASE "ERASE secondary image and reset device\n"
#define LWM2M_HELP_RESET "Reset device\n"
#define LWM2M_HELP_SHEDULE "Set Test flag to image\n"
#define LWM2M_HELP_READ "Read image list\n"

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_mcumgr,
	SHELL_CMD_ARG(download, NULL, LWM2M_HELP_DOWNLOAD, cmd_download, 1, 1),
        SHELL_CMD_ARG(update, NULL, LWM2M_HELP_SHEDULE, cmd_update, 1, 0),
        SHELL_CMD_ARG(erase, NULL, LWM2M_HELP_ERASE, cmd_erase, 1, 0),
        SHELL_CMD_ARG(reset, NULL, LWM2M_HELP_RESET, cmd_reset, 1, 0),
        SHELL_CMD_ARG(read, NULL, LWM2M_HELP_READ, cmd_read_image_list, 1, 0),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_ARG_REGISTER(mcumgr, &sub_mcumgr,
			    LWM2M_HELP_CMD, NULL, 1, 0);
