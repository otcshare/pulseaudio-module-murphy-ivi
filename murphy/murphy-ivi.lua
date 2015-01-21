zone { name = "driver" }
zone { name = "passanger1" }
zone { name = "passanger2" }
zone { name = "passanger3" }
zone { name = "passanger4" }

routing_group {
    name = "default_driver",
    node_type = node.output,
    accept = function(self, n)
        return (n.type ~= node.bluetooth_carkit and n.type ~= node.hdmi)
    end,
    compare = builtin.method.compare_default
}

routing_group {
    name = "default_driver",
    node_type = node.input,
    accept = function(self, n)
        return (n.type == node.microphone)
    end,
    compare = builtin.method.compare_default
}

routing_group {
    name = "default_passanger1",
    node_type = node.output,
    accept = function(self, n)
        return (n.type == node.hdmi or n.name == 'Silent')
    end,
    compare = builtin.method.compare_default
}

routing_group {
    name = "phone",
    node_type = node.input,
    accept = builtin.method.accept_phone,
    compare = builtin.method.compare_phone
}

routing_group {
    name = "phone",
    node_type = node.output,
    accept = builtin.method.accept_phone,
    compare = builtin.method.compare_phone
}

application_class {
    class = "event",
    node_type = node.event,
    priority = 7,
    route = {
        output = { driver = routing_group.default_driver_output }
    },
    roles = { event  = no_resource }
}

application_class {
    class = "phone",
    node_type = node.phone,
    priority = 6,
    route = {
        input  = { driver = routing_group.phone_input },
        output = {driver = routing_group.phone_output }
    },
    roles = { phone = no_resource, carkit = no_resource }
}

application_class {
    node_type = node.alert,
    priority = 5,
    route = {
        output = { driver = routing_group.default_driver_output },
    },
    roles = { ringtone = no_resource, alarm = no_resource }
}

application_class {
    class = "navigator",
    node_type = node.navigator,
    priority = 4,
    route = {
        output = { driver = routing_group.default_driver_output,
               passanger1 = routing_group.default_passanger1_output }
    },
    roles = { navigator = {0, "autorelease", "mandatory", "shared"}, speech = no_resource },
    binaries = { ['net.zmap.navi'] = { 0, "autorelease", "mandatory", "shared" } }
}

application_class {
    class = "game",
    node_type = node.game,
    priority = 3,
    route = {
        output = { driver = routing_group.default_driver_output,
               passanger1 = routing_group.default_passanger1_output }
    },
    roles = { game = {0, "mandatory", "exclusive"} }
}

application_class {
    class = "player",
    node_type = node.radio,
    priority = 2,
    route = {
        output = { driver = routing_group.default_driver_output }
    },
    roles = { radio = {1, "mandatory", "exclusive"} },
}

application_class {
    class = "player",
    node_type = node.player,
    priority = 2,
    route = {
        output = { driver = routing_group.default_driver_output,
                   passanger1 = routing_group.default_passanger1_output }
    },
    roles = { music    = {0, "mandatory", "exclusive"},
              video    = {0, "mandatory", "exclusive"},
              test     = {0, "mandatory", "exclusive"},
              bt_music = no_resource,
              player   = no_resource
    },
    binaries = { ['t8j6HTRpuz.MediaPlayer'] = "music" }
}

application_class {
    class = "player",
    node_type = node.browser,
    priority = 2,
    route = {
        output = { driver = routing_group.default_driver_output,
               passanger1 = routing_group.default_passanger1_output }
    },
    roles = { browser = {0, "mandatory", "shared"} }
}

application_class {
    class = "unspecified_output_stream",
    node_type = node.unspecified_output_stream,
    priority = 1,
    route = {
        input = { driver = routing_group.default_driver_input }
    }
}


audio_resource {
    name = { recording = "audio_recording", playback = "audio_playback" },
    attributes = {
       role = {"media.role", mdb.string, "music"},
       pid  = {"application.process.id", mdb.string, "<unknown>"},
       name = {"resource.set.name", mdb.string, "<unknown>"},
       appid = {"resource.set.appid", mdb.string, "<unknown>"}
    }
}

mdb.import {
    table = "speedvol",
    columns = {"value"},
    condition = "zone = 'driver' AND device = 'speaker'",
    maxrow = 1,
    update = builtin.method.make_volumes
}

mdb.import {
    table = "audio_playback_owner",
    columns = {"zone_id", "application_class", "role"},
    condition = "zone_name = 'driver'",
    maxrow = 1,
    update = function(self)
        zid = self[1].zone_id
    if (zid == nil) then zid = "<nil>" end
    class = self[1].application_class
    if (class == nil) then class = "<nil>" end
    role = self[1].role
    if (role == nil) then role = "<nil>" end
--      print("*** import "..self.table.." update: zone:"..zid.." class:"..class.." role:"..role)
    end
}

mdb.import {
    table = "amb_gear_position",
    columns = { "value" },
    condition = "key = 'GearPosition'",
    maxrow = 1,
    update = builtin.method.make_volumes
}

mdb.import {
    table = "volume_context",
    columns = {"value"},
    condition = "id = 1",
    maxrow = 1,
    update = builtin.method.change_volume_context
}

volume_limit {
    name = "speed_adjust",
    type = volume_limit.generic,
    limit = mdb.import.speedvol:link(1,"value"),
    calculate = builtin.method.volume_correct
}

volume_limit {
    name = "phone_suppress",
    type = volume_limit.class,
    limit = -20,
    node_type = { node.phone },
    calculate = builtin.method.volume_supress
}


volume_limit {
    name = "navi_suppress",
    type = volume_limit.class,
    limit = -20,
    node_type = { node.navigator, node.phone },
    calculate = builtin.method.volume_supress
}

volume_limit {
    name = "navi_maxlim",
    type = volume_limit.maximum,
    limit = -10,
    node_type = { node.navigator }
}

volume_limit {
    name = "player_maxlim",
    type = volume_limit.maximum,
    limit = -20,
    node_type = { node.player }
}

volume_limit {
    name = "video",
    type = volume_limit.class,
    limit = -90,
    node_type = { node.player, node.game },
    calculate = function(self, class, device, mask)
--      print("*** limit "..self.name.." class:"..class.." stream:"..device.name)
        position = mdb.import.amb_gear_position[1].value
        if (position and position == 128) then
            return self.limit
        end
        return 0
    end
}


