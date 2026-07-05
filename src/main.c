/*
 * USB HID keyboard bridge for NUCLEO-U575ZI-Q.
 *
 * Bytes typed into a terminal on USART1 (ST-Link VCP, CN1) are translated to
 * HID boot-keyboard reports and sent out the OTG FS USB device (CN15).
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_hid.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* ---------------------------------------------------------------------- */
/* USB device context                                                     */
/* ---------------------------------------------------------------------- */

#define HIDBRIDGE_VID 0x2FE3
#define HIDBRIDGE_PID 0x0006

USBD_DEVICE_DEFINE(hidbridge_usbd,
		    DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
		    HIDBRIDGE_VID, HIDBRIDGE_PID);

USBD_DESC_LANG_DEFINE(hidbridge_lang);
USBD_DESC_MANUFACTURER_DEFINE(hidbridge_mfr, "Zephyr Project");
USBD_DESC_PRODUCT_DEFINE(hidbridge_product, "HID Bridge");
USBD_DESC_CONFIG_DEFINE(hidbridge_fs_cfg_desc, "FS Configuration");

USBD_CONFIGURATION_DEFINE(hidbridge_fs_config, USB_SCD_SELF_POWERED, 125,
			   &hidbridge_fs_cfg_desc);

static struct usbd_context *hidbridge_usbd_setup(usbd_msg_cb_t msg_cb)
{
	int err;

	err = usbd_add_descriptor(&hidbridge_usbd, &hidbridge_lang);
	if (err) {
		LOG_ERR("Failed to add language descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&hidbridge_usbd, &hidbridge_mfr);
	if (err) {
		LOG_ERR("Failed to add manufacturer descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&hidbridge_usbd, &hidbridge_product);
	if (err) {
		LOG_ERR("Failed to add product descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_configuration(&hidbridge_usbd, USBD_SPEED_FS,
				      &hidbridge_fs_config);
	if (err) {
		LOG_ERR("Failed to add Full-Speed configuration (%d)", err);
		return NULL;
	}

	err = usbd_register_all_classes(&hidbridge_usbd, USBD_SPEED_FS, 1, NULL);
	if (err) {
		LOG_ERR("Failed to register classes (%d)", err);
		return NULL;
	}

	usbd_device_set_code_triple(&hidbridge_usbd, USBD_SPEED_FS, 0, 0, 0);
	usbd_self_powered(&hidbridge_usbd, true);

	if (msg_cb != NULL) {
		err = usbd_msg_register_cb(&hidbridge_usbd, msg_cb);
		if (err) {
			LOG_ERR("Failed to register message callback (%d)", err);
			return NULL;
		}
	}

	err = usbd_init(&hidbridge_usbd);
	if (err) {
		LOG_ERR("Failed to initialize USB device support (%d)", err);
		return NULL;
	}

	return &hidbridge_usbd;
}

static void usbd_msg_cb(struct usbd_context *const ctx,
			 const struct usbd_msg *const msg)
{
	LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));

	if (msg->type == USBD_MSG_CONFIGURATION) {
		LOG_INF("\tConfiguration value %d", msg->status);
	}

	if (usbd_can_detect_vbus(ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			if (usbd_enable(ctx)) {
				LOG_ERR("Failed to enable device support");
			}
		}

		if (msg->type == USBD_MSG_VBUS_REMOVED) {
			if (usbd_disable(ctx)) {
				LOG_ERR("Failed to disable device support");
			}
		}
	}
}

/* ---------------------------------------------------------------------- */
/* HID keyboard report                                                    */
/* ---------------------------------------------------------------------- */

static const uint8_t hid_report_desc[] = HID_KEYBOARD_REPORT_DESC();

enum kb_report_idx {
	KB_MOD_KEY = 0,
	KB_RESERVED,
	KB_KEY_CODE1,
	KB_KEY_CODE2,
	KB_KEY_CODE3,
	KB_KEY_CODE4,
	KB_KEY_CODE5,
	KB_KEY_CODE6,
	KB_REPORT_COUNT,
};

UDC_STATIC_BUF_DEFINE(report, KB_REPORT_COUNT);

static bool kb_ready;

static void kb_iface_ready(const struct device *dev, const bool ready)
{
	LOG_INF("HID device %s interface is %s", dev->name, ready ? "ready" : "not ready");
	kb_ready = ready;
}

static int kb_get_report(const struct device *dev, const uint8_t type,
			  const uint8_t id, const uint16_t len, uint8_t *const buf)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(type);
	ARG_UNUSED(id);
	ARG_UNUSED(len);
	ARG_UNUSED(buf);
	return 0;
}

static int kb_verify_set_report(const struct device *dev, const uint8_t type,
				  const uint8_t id, const uint16_t len)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(type);
	ARG_UNUSED(id);
	ARG_UNUSED(len);
	return -ENOTSUP;
}

static int kb_set_report(const struct device *dev, const uint8_t type,
			   const uint8_t id, const uint16_t len,
			   const uint8_t *const buf)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(type);
	ARG_UNUSED(id);
	ARG_UNUSED(len);
	ARG_UNUSED(buf);
	return 0;
}

static void kb_set_idle(const struct device *dev, const uint8_t id, const uint32_t duration)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(id);
	ARG_UNUSED(duration);
}

static uint32_t kb_get_idle(const struct device *dev, const uint8_t id)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(id);
	return 0;
}

static void kb_set_protocol(const struct device *dev, const uint8_t proto)
{
	ARG_UNUSED(dev);
	LOG_INF("Protocol changed to %s", proto == 0U ? "Boot Protocol" : "Report Protocol");
}

static struct hid_device_ops kb_ops = {
	.iface_ready = kb_iface_ready,
	.get_report = kb_get_report,
	.verify_set_report = kb_verify_set_report,
	.set_report = kb_set_report,
	.set_idle = kb_set_idle,
	.get_idle = kb_get_idle,
	.set_protocol = kb_set_protocol,
};

/* ---------------------------------------------------------------------- */
/* UART RX -> byte queue                                                  */
/* ---------------------------------------------------------------------- */

#define UART_DEVICE_NODE DT_CHOSEN(zephyr_console)
static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

K_MSGQ_DEFINE(rx_msgq, sizeof(uint8_t), 64, 1);

static void uart_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	ARG_UNUSED(user_data);

	uart_irq_update(dev);

	if (!uart_irq_rx_ready(dev)) {
		return;
	}

	while (uart_fifo_read(dev, &c, 1) == 1) {
		/* Queue full: drop the byte silently. */
		(void)k_msgq_put(&rx_msgq, &c, K_NO_WAIT);
	}
}

/* ---------------------------------------------------------------------- */
/* ASCII -> HID keycode/modifier translation                              */
/* ---------------------------------------------------------------------- */

#define HID_MOD_NONE       0x00
#define HID_MOD_LEFT_CTRL  0x01
#define HID_MOD_LEFT_SHIFT 0x02

#define HID_KEY_ENTER     0x28
#define HID_KEY_ESC       0x29
#define HID_KEY_BACKSPACE 0x2A
#define HID_KEY_TAB       0x2B
#define HID_KEY_SPACE     0x2C

#define HID_KEY_INSERT   0x49
#define HID_KEY_HOME     0x4A
#define HID_KEY_PAGE_UP  0x4B
#define HID_KEY_DELETE   0x4C
#define HID_KEY_END      0x4D
#define HID_KEY_PAGE_DN  0x4E
#define HID_KEY_RIGHT 0x4F
#define HID_KEY_LEFT  0x50
#define HID_KEY_DOWN  0x51
#define HID_KEY_UP    0x52

static bool ascii_to_hid(uint8_t c, uint8_t *keycode, uint8_t *modifier)
{
	*modifier = HID_MOD_NONE;

	switch (c) {
	case '\r':
	case '\n':
		*keycode = HID_KEY_ENTER;
		return true;
	case '\t':
		*keycode = HID_KEY_TAB;
		return true;
	case 0x7f: /* Backspace (DEL) as sent by most terminals */
	case 0x08: /* Backspace (BS) */
		*keycode = HID_KEY_BACKSPACE;
		return true;
	case 0x03: /* Ctrl+C */
		*modifier = HID_MOD_LEFT_CTRL;
		*keycode = 0x06; /* 'c' */
		return true;
	case ' ':
		*keycode = HID_KEY_SPACE;
		return true;
	}

	if (c >= 'a' && c <= 'z') {
		*keycode = 0x04 + (c - 'a');
		return true;
	}

	if (c >= 'A' && c <= 'Z') {
		*modifier = HID_MOD_LEFT_SHIFT;
		*keycode = 0x04 + (c - 'A');
		return true;
	}

	if (c >= '1' && c <= '9') {
		*keycode = 0x1E + (c - '1');
		return true;
	}

	if (c == '0') {
		*keycode = 0x27;
		return true;
	}

	switch (c) {
	case '-':
		*keycode = 0x2D;
		return true;
	case '=':
		*keycode = 0x2E;
		return true;
	case '[':
		*keycode = 0x2F;
		return true;
	case ']':
		*keycode = 0x30;
		return true;
	case '\\':
		*keycode = 0x31;
		return true;
	case ';':
		*keycode = 0x33;
		return true;
	case '\'':
		*keycode = 0x34;
		return true;
	case '`':
		*keycode = 0x35;
		return true;
	case ',':
		*keycode = 0x36;
		return true;
	case '.':
		*keycode = 0x37;
		return true;
	case '/':
		*keycode = 0x38;
		return true;
	}

	/* Shifted symbols (US layout) share keycodes with their unshifted key. */
	switch (c) {
	case '!':
		*keycode = 0x1E; /* shift+1 */
		break;
	case '@':
		*keycode = 0x1F; /* shift+2 */
		break;
	case '#':
		*keycode = 0x20; /* shift+3 */
		break;
	case '$':
		*keycode = 0x21; /* shift+4 */
		break;
	case '%':
		*keycode = 0x22; /* shift+5 */
		break;
	case '^':
		*keycode = 0x23; /* shift+6 */
		break;
	case '&':
		*keycode = 0x24; /* shift+7 */
		break;
	case '*':
		*keycode = 0x25; /* shift+8 */
		break;
	case '(':
		*keycode = 0x26; /* shift+9 */
		break;
	case ')':
		*keycode = 0x27; /* shift+0 */
		break;
	case '_':
		*keycode = 0x2D; /* shift+- */
		break;
	case '+':
		*keycode = 0x2E; /* shift+= */
		break;
	case '{':
		*keycode = 0x2F; /* shift+[ */
		break;
	case '}':
		*keycode = 0x30; /* shift+] */
		break;
	case '|':
		*keycode = 0x31; /* shift+\ */
		break;
	case ':':
		*keycode = 0x33; /* shift+; */
		break;
	case '"':
		*keycode = 0x34; /* shift+' */
		break;
	case '~':
		*keycode = 0x35; /* shift+` */
		break;
	case '<':
		*keycode = 0x36; /* shift+, */
		break;
	case '>':
		*keycode = 0x37; /* shift+. */
		break;
	case '?':
		*keycode = 0x38; /* shift+/ */
		break;
	default:
		return false;
	}

	*modifier = HID_MOD_LEFT_SHIFT;
	return true;
}

/* ---------------------------------------------------------------------- */
/* ANSI escape sequence parsing (arrow keys etc.)                         */
/* ---------------------------------------------------------------------- */

/*
 * Terminals send arrow/navigation keys as multi-byte CSI sequences
 * (ESC '[' <final-byte>), not as single ASCII codes, so ascii_to_hid()
 * alone can't see them. This tracks how far into such a sequence we are.
 */
#define ANSI_PARAM_MAX_LEN 8

enum esc_state {
	ESC_STATE_NONE = 0,
	ESC_STATE_GOT_ESC,
	ESC_STATE_GOT_CSI,
	ESC_STATE_GOT_SS3,
};

static uint16_t ansi_first_param(const uint8_t *params, size_t len)
{
	uint16_t value = 0;

	for (size_t i = 0; i < len; i++) {
		if (params[i] < '0' || params[i] > '9') {
			break;
		}

		value = (value * 10U) + (uint16_t)(params[i] - '0');
	}

	return value;
}

static bool ansi_arrow_to_hid(uint8_t c, uint8_t *keycode)
{
	switch (c) {
	case 'A':
		*keycode = HID_KEY_UP;
		return true;
	case 'B':
		*keycode = HID_KEY_DOWN;
		return true;
	case 'C':
		*keycode = HID_KEY_RIGHT;
		return true;
	case 'D':
		*keycode = HID_KEY_LEFT;
		return true;
	}

	return false;
}

static bool csi_to_hid(const uint8_t *params, size_t len, uint8_t final, uint8_t *keycode)
{
	uint16_t param;

	if (ansi_arrow_to_hid(final, keycode)) {
		return true;
	}

	if (final != '~') {
		return false;
	}

	param = ansi_first_param(params, len);
	switch (param) {
	case 1:
	case 7:
		*keycode = HID_KEY_HOME;
		return true;
	case 2:
		*keycode = HID_KEY_INSERT;
		return true;
	case 3:
		*keycode = HID_KEY_DELETE;
		return true;
	case 4:
	case 8:
		*keycode = HID_KEY_END;
		return true;
	case 5:
		*keycode = HID_KEY_PAGE_UP;
		return true;
	case 6:
		*keycode = HID_KEY_PAGE_DN;
		return true;
	default:
		return false;
	}
}

static bool ss3_to_hid(uint8_t c, uint8_t *keycode)
{
	if (ansi_arrow_to_hid(c, keycode)) {
		return true;
	}

	switch (c) {
	case 'H':
		*keycode = HID_KEY_HOME;
		return true;
	case 'F':
		*keycode = HID_KEY_END;
		return true;
	default:
		return false;
	}
}

/* ---------------------------------------------------------------------- */

static void send_hid_key(const struct device *hid_dev, uint8_t modifier, uint8_t keycode)
{
	int ret;

	memset(report, 0, KB_REPORT_COUNT);
	report[KB_MOD_KEY] = modifier;
	report[KB_KEY_CODE1] = keycode;
	ret = hid_device_submit_report(hid_dev, KB_REPORT_COUNT, report);
	if (ret) {
		LOG_ERR("HID submit report error (%d)", ret);
	}

	k_msleep(2);

	memset(report, 0, KB_REPORT_COUNT);
	ret = hid_device_submit_report(hid_dev, KB_REPORT_COUNT, report);
	if (ret) {
		LOG_ERR("HID submit report error (%d)", ret);
	}
}

int main(void)
{
	const struct device *hid_dev;
	struct usbd_context *usbd_ctx;
	int ret;

	if (!device_is_ready(uart_dev)) {
		LOG_ERR("UART device not ready");
		return -EIO;
	}

	hid_dev = DEVICE_DT_GET_ONE(zephyr_hid_device);
	if (!device_is_ready(hid_dev)) {
		LOG_ERR("HID device not ready");
		return -EIO;
	}

	ret = hid_device_register(hid_dev, hid_report_desc, sizeof(hid_report_desc), &kb_ops);
	if (ret != 0) {
		LOG_ERR("Failed to register HID device (%d)", ret);
		return ret;
	}

	usbd_ctx = hidbridge_usbd_setup(usbd_msg_cb);
	if (usbd_ctx == NULL) {
		LOG_ERR("Failed to initialize USB device");
		return -ENODEV;
	}

	if (!usbd_can_detect_vbus(usbd_ctx)) {
		ret = usbd_enable(usbd_ctx);
		if (ret) {
			LOG_ERR("Failed to enable USB device support (%d)", ret);
			return ret;
		}
	}

	ret = uart_irq_callback_user_data_set(uart_dev, uart_cb, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to set UART callback (%d)", ret);
		return ret;
	}
	uart_irq_rx_enable(uart_dev);

	LOG_INF("HID bridge ready");

	enum esc_state esc_state = ESC_STATE_NONE;
	uint8_t ansi_params[ANSI_PARAM_MAX_LEN];
	size_t ansi_param_len = 0;
	bool suppress_lf = false;

	while (true) {
		uint8_t c;
		uint8_t keycode;
		uint8_t modifier;

		/*
		 * While parsing a possible escape sequence, use a short timeout
		 * so a lone Escape keypress (no following bytes) still gets
		 * delivered instead of hanging forever waiting for more bytes.
		 */
		k_timeout_t timeout = (esc_state == ESC_STATE_NONE) ? K_FOREVER : K_MSEC(50);

		if (k_msgq_get(&rx_msgq, &c, timeout) != 0) {
			/* Timed out mid-sequence: treat the pending ESC as a literal key. */
			esc_state = ESC_STATE_NONE;
			if (kb_ready) {
				send_hid_key(hid_dev, HID_MOD_NONE, HID_KEY_ESC);
			}
			continue;
		}

		if (esc_state == ESC_STATE_NONE && c == 0x1b) {
			esc_state = ESC_STATE_GOT_ESC;
			ansi_param_len = 0;
			suppress_lf = false;
			continue;
		}

		if (esc_state == ESC_STATE_GOT_ESC) {
			esc_state = ESC_STATE_NONE;
			if (c == '[') {
				esc_state = ESC_STATE_GOT_CSI;
				continue;
			}
			if (c == 'O') {
				esc_state = ESC_STATE_GOT_SS3;
				continue;
			}
			/* Not a recognized CSI sequence: drop the ESC, fall through to c. */
		}

		if (esc_state == ESC_STATE_GOT_CSI) {
			if (c >= 0x30 && c <= 0x3f) {
				if (ansi_param_len < sizeof(ansi_params)) {
					ansi_params[ansi_param_len++] = c;
				}
				continue;
			}

			if (c >= 0x20 && c <= 0x2f) {
				continue;
			}

			esc_state = ESC_STATE_NONE;
			if (c >= 0x40 && c <= 0x7e &&
			    kb_ready && csi_to_hid(ansi_params, ansi_param_len, c, &keycode)) {
				send_hid_key(hid_dev, HID_MOD_NONE, keycode);
			}
			continue;
		}

		if (esc_state == ESC_STATE_GOT_SS3) {
			esc_state = ESC_STATE_NONE;
			if (kb_ready && ss3_to_hid(c, &keycode)) {
				send_hid_key(hid_dev, HID_MOD_NONE, keycode);
			}
			continue;
		}

		if (c == '\n' && suppress_lf) {
			suppress_lf = false;
			continue;
		}

		suppress_lf = false;

		if (!ascii_to_hid(c, &keycode, &modifier)) {
			continue;
		}

		if (!kb_ready) {
			continue;
		}

		send_hid_key(hid_dev, modifier, keycode);
		suppress_lf = (c == '\r');
	}

	return 0;
}
