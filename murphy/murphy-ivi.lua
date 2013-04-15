
routing_group {
    name = "default",
    node_type = node.output,
    accept = builtin.method.accept_default,
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
    node_type = node.event,
    priority = 6,
    route = {
        output = routing_group.default_output
    },
    roles = { event = no_resource }
}

application_class {
    class = "phone",
    node_type = node.phone,
    priority = 5,
    route = {
        input = routing_group.phone_input,
        output = routing_group.phone_output
    },
    roles = { phone = no_resource, carkit = no_resource }
}

application_class {
    node_type = node.alert,
    priority = 4,
    route = {
        output = routing_group.default_output
    },
    roles = { ringtone = no_resource, alarm = no_resource }
}

application_class {
    class = "navigator",
    node_type = node.navigator,
    priority = 3,
    route = {
        output = routing_group.default_output
    },
    roles = { navigator = {0, "autorelease", "mandatory", "shared"} }
}

application_class {
    class = "game",
    node_type = node.game,
    priority = 2,
    route = {
        output = routing_group.default_output
    },
    roles = { game = {0, "mandatory", "exclusive"} }
}

application_class {
    class = "player",
    node_type = node.radio,
    priority = 1,
    route = {
        output = routing_group.default_output
    },
    roles = { radio = {1, "mandatory", "exclusive"} },
}

application_class {
    class = "player",
    node_type = node.player,
    priority = 1,
    route = {
        output = routing_group.default_output
    },
    roles = { music   = {0, "mandatory", "exclusive"},
              video   = {0, "mandatory", "exclusive"},
	      test    = {0, "mandatory", "exclusive"}
    }
}

application_class {
    class = "player",
    node_type = node.browser,
    priority = 1,
    route = {
        output = routing_group.default_output
    },
    roles = { browser = {0, "mandatory", "shared"} }
}



audio_resource {
    name = { recording = "audio_recording", playback = "audio_playback" },
    attributes = {
       role = {"media.role", mdb.string, "music"},
       pid  = {"application.process.id", mdb.string, "<unknown>"}
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
    table = "amb_shift_position",
    columns = {"shift_position"},
    condition = "id = 0",
    maxrow = 1,
    update = builtin.method.make_volumes
}

volume_limit {
    name = "speed_adjust",
    type = volume_limit.generic,
    limit = mdb.import.speedvol:link(1,"value"),
    calculate = builtin.method.volume_correct
}

volume_limit {
    name = "suppress",
    type = volume_limit.class,
    limit = -20;
    node_type = { node.phone, node.navigator },
    calculate = builtin.method.volume_supress
}

volume_limit {
    name = "video",
    type = volume_limit.class,
    limit = -90,
    node_type = { node.player, node.game },
    calculate = function(self, class, device)
--    	print("*** limit "..self.name.." class:"..class.." stream:"..device.name)
    	position = mdb.import.amb_shift_position[1].shift_position
    	if (position  and position == 128) then
    	    return self.limit
    	end
    	return 0
    end
}


