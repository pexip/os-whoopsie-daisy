/* whoopsie
 * 
 * Copyright © 2011-2012 Canonical Ltd.
 * Author: Evan Dandrea <evan.dandrea@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define _XOPEN_SOURCE

#include <stdlib.h>
#include <gio/gio.h>
#include <glib.h>
#include <NetworkManager/NetworkManager.h>
#include "connectivity.h"

#define NETWORK_MANAGER_SERVICE "org.freedesktop.NetworkManager"
#define NETWORK_MANAGER_OBJECT "/org/freedesktop/NetworkManager"
#define NETWORK_MANAGER_INTERFACE "org.freedesktop.NetworkManager"
#define NETWORK_MANAGER_ACTIVE_INTERFACE "org.freedesktop.NetworkManager.Connection.Active"
#define NETWORK_MANAGER_DEVICE_INTERFACE "org.freedesktop.NetworkManager.Device"
#define DBUS_PROPERTIES_INTERFACE "org.freedesktop.DBus.Properties"

static gboolean route_available = FALSE;
static gboolean network_available = FALSE;

struct _connectivity_data {
    ConnectionAvailableCallback callback;
    const char* url;
};

static struct _connectivity_data connectivity_data;

gboolean
is_default_route (GDBusConnection* connection, const gchar* path, gboolean v6)
{
    /* Whether this active connection is the default IPv4 or IPv6 connection.
     */
    GVariant* properties = NULL;
    GVariant* result = NULL;
    guint value = 0;
    GError* err = NULL;

    result = g_dbus_connection_call_sync (connection,
                                 NETWORK_MANAGER_SERVICE,
                                 path,
                                 DBUS_PROPERTIES_INTERFACE,
                                 "Get",
                                 g_variant_new ("(ss)",
                                    NETWORK_MANAGER_ACTIVE_INTERFACE,
                                    v6 ?  "Default6" : "Default"),
                                 G_VARIANT_TYPE ("(v)"),
                                 G_DBUS_CALL_FLAGS_NONE,
                                 3000,
                                 NULL,
                                 &err);
    if (!result) {
        g_print ("Could not determine if this is a default route: %s\n",
                   err->message);
        return FALSE;
    }
    g_variant_get (result, "(v)", &properties);
    g_variant_get (properties, "b", &value);

    g_variant_unref (result);
    g_variant_unref (properties);

    return value;
}

gboolean
device_is_paid_data_plan (GDBusConnection* connection, const gchar* path)
{
    /* Whether this device is a type often associated with paid data plans */
    GError* err = NULL;
    GVariant* result = NULL;
    GVariant* properties = NULL;
    guint device_type;
    result = g_dbus_connection_call_sync (connection,
                                  NETWORK_MANAGER_SERVICE,
                                  path,
                                  DBUS_PROPERTIES_INTERFACE,
                                  "Get",
                                  g_variant_new ("(ss)",
                                      NETWORK_MANAGER_DEVICE_INTERFACE,
                                      "DeviceType"),
                                  G_VARIANT_TYPE ("(v)"),
                                  G_DBUS_CALL_FLAGS_NONE,
                                  3000,
                                  NULL,
                                  &err);
    if (!result) {
        g_print ("Could not get the device type of %s: %s\n",
                   path, err->message);
        return FALSE;
    }
    g_variant_get (result, "(v)", &properties);
    g_variant_get (properties, "u", &device_type);
    /* We're on a connection that is potentially billed for the data
     * used (3G, dial-up modem, WIMAX). Bail out. */
    if (device_type > NM_DEVICE_TYPE_WIFI) {
        g_variant_unref (result);
        return TRUE;
    }
    g_variant_unref (result);
    return FALSE;
}
gboolean
is_paid_data_plan (GDBusConnection* connection, const gchar* path)
{
    GError* err = NULL;
    GVariant* result = NULL;
    GVariant* properties = NULL;
    GVariantIter* iter = NULL;
    gchar* device_path;
    result = g_dbus_connection_call_sync (connection,
                                          NETWORK_MANAGER_SERVICE,
                                          path,
                                          DBUS_PROPERTIES_INTERFACE,
                                          "Get",
                                          g_variant_new ("(ss)",
                                              NETWORK_MANAGER_ACTIVE_INTERFACE,
                                              "Devices"),
                                          G_VARIANT_TYPE ("(v)"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          3000,
                                          NULL,
                                          &err);
    if (!result) {
        g_print ("Could not get the devices for %s: %s\n",
                   path, err->message);
        return FALSE;
    }
    g_variant_get (result, "(v)", &properties);
    g_variant_get (properties, "ao", &iter);
    while (g_variant_iter_loop (iter, "&o", &device_path)) {
        if (device_is_paid_data_plan (connection, device_path)) {
            g_variant_iter_free (iter);
            g_variant_unref (result);
            return TRUE;
        }
    }
    g_variant_iter_free (iter);
    g_variant_unref (result);
    return FALSE;
}

void
network_manager_state_changed (GDBusConnection* connection,
                               const gchar* sender_name, const gchar*
                               object_path, const gchar* interface_name,
                               const gchar* signal_name, GVariant* parameters,
                               gpointer user_data)
{
    GVariant* result = NULL;
    GVariant* properties = NULL;
    GVariantIter* iter = NULL;
    GError* err = NULL;
    gchar* path = NULL;
    guint32 connected_state;
    gboolean paid = TRUE;
    ConnectionAvailableCallback callback = user_data;

    if (!parameters)
        return;
    
    g_variant_get_child (parameters, 0, "u", &connected_state);
    if (NM_STATE_CONNECTED_GLOBAL != connected_state) {
        network_available = FALSE;
        callback (network_available);
        return;
    }

    result = g_dbus_connection_call_sync (connection,
                                          NETWORK_MANAGER_SERVICE,
                                          NETWORK_MANAGER_OBJECT,
                                          DBUS_PROPERTIES_INTERFACE,
                                          "Get",
                                          g_variant_new ("(ss)",
                                              NETWORK_MANAGER_INTERFACE,
                                              "ActiveConnections"),
                                          G_VARIANT_TYPE ("(v)"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          3000,
                                          NULL,
                                          &err);
    if (!result) {
        g_print ("Could not get the list of active connections: %s\n",
                   err->message);
        return;
    }

    g_variant_get (result, "(v)", &properties);
    g_variant_get (properties, "ao", &iter);
    while (g_variant_iter_loop (iter, "&o", &path)) {
        if (is_default_route (connection, path, FALSE) ||
            is_default_route (connection, path, TRUE)) {
            if (!is_paid_data_plan (connection, path)) {
                paid = FALSE;
                break;
            }
        }
    }

    g_variant_iter_free (iter);
    g_variant_unref (result);

    network_available = !paid;
    callback (network_available && route_available);
}

void
route_changed (GNetworkMonitor *nm, gboolean available, gpointer user_data)
{
    GError* err = NULL;
    GSocketConnectable *addr = NULL;

    addr = g_network_address_parse_uri (connectivity_data.url, 80, &err);
    if (!addr) {
        g_print ("Could not parse crash database URL: %s\n", err->message);
        g_error_free (err);
        return;
    }

    route_available = (available &&
                       g_network_monitor_can_reach (nm, addr, NULL, NULL));

    if (network_available && route_available)
        connectivity_data.callback (TRUE);

    g_object_unref (addr);
}

void
setup_network_route_monitor (void)
{
    GNetworkMonitor* nm = NULL;
    GSocketConnectable *addr = NULL;
    GError* err = NULL;

    /* Using GNetworkMonitor brings in GSettings, which brings in DConf, which
     * brings in a DBus session bus, which brings in pain. */
    if (putenv ("GSETTINGS_BACKEND=memory") != 0)
        g_print ("Could not set the GSettings backend to memory.\n");

    addr = g_network_address_parse_uri (connectivity_data.url, 80, &err);
    if (!addr) {
        g_print ("Could not parse crash database URL: %s\n", err->message);
        g_error_free (err);
        return;
    }

    nm = g_network_monitor_get_default ();
    if (!nm)
        return;

    route_available = (g_network_monitor_get_network_available (nm) &&
                       g_network_monitor_can_reach (nm, addr, NULL, NULL));

    g_signal_connect (nm, "network-changed", G_CALLBACK (route_changed), NULL);
    g_object_unref (addr);
}

gboolean
monitor_connectivity (const char* crash_url, ConnectionAvailableCallback callback)
{
    GDBusConnection* system_bus = NULL;
    GError* err = NULL;
    GVariant* result = NULL;
    GVariant* properties = NULL;
    GVariant* current_state = NULL;
    guint value = 0;
    
    connectivity_data.url = crash_url;
    connectivity_data.callback = callback;
    /* Checking whether a NetworkManager connection is not enough.
     * NetworkManager will report CONNECTED_GLOBAL when a route is not present
     * when the connectivity option is not set. We'll use GNetworkMonitor here
     * to fill in the gap. */
    setup_network_route_monitor ();

    system_bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &err);
    if (err) {
        g_print ("Could not connect to the system bus: %s\n", err->message);
        g_error_free (err);
        return FALSE;
    }

    g_dbus_connection_signal_subscribe (system_bus,
                                        NETWORK_MANAGER_SERVICE,
                                        NETWORK_MANAGER_INTERFACE,
                                        "StateChanged",
                                        NULL,
                                        NULL,
                                        (GDBusSignalFlags) NULL,
                                        network_manager_state_changed,
                                        callback,
                                        NULL);

    if (err) {
        g_print ("Could not monitor Network Manager: %s\n", err->message);
        g_error_free (err);
        return FALSE;
    }

    result = g_dbus_connection_call_sync (system_bus,
                                          NETWORK_MANAGER_SERVICE,
                                          NETWORK_MANAGER_OBJECT,
                                          DBUS_PROPERTIES_INTERFACE,
                                          "Get",
                                          g_variant_new ("(ss)",
                                                     NETWORK_MANAGER_INTERFACE,
                                                     "state"),
                                          NULL,
                                          G_DBUS_CALL_FLAGS_NONE,
                                          3000,
                                          NULL,
                                          &err);
    if (!result) {
        g_print ("Could not get the Network Manager state: %s\n", err->message);
        g_error_free (err);
        return TRUE;
    }

    g_variant_get (result, "(v)", &properties);
    g_variant_get (properties, "u", &value);
    current_state = g_variant_new ("(u)", value);
    network_manager_state_changed (system_bus, NULL, NULL, NULL, NULL,
                                   current_state, callback);
    g_variant_ref_sink (current_state);
    g_variant_unref (current_state);
    g_variant_unref (result);

    return TRUE;
}
