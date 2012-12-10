
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
    node_type = node.phone,
    priority = 4,
    route = {
        input = routing_group.phone_input,
        output = routing_group.phone_output
    }
}

application_class {
    node_type = node.radio,
    priority = 1,
    route = {
        output = routing_group.default_output
    }
}

application_class {
    node_type = node.player,
    priority = 1,
    route = {
        output = routing_group.default_output
    }
}

application_class {
    node_type = node.navigator,
    priority = 2,
    route = {
        output = routing_group.default_output
    }
}

application_class {
    node_type = node.game,
    priority = 3,
    route = {
        output = routing_group.default_output
    }
}

application_class {
    node_type = node.browser,
    priority = 1,
    route = {
        output = routing_group.default_output
    }
}

application_class {
    node_type = node.event,
    priority = 5,
    route = {
        output = routing_group.default_output
    }
}

volume_limit {
    name = "speed_adjust",
    type = volume_limit.generic,
    limit = -10;
    calculate = builtin.method.volume_correct
}

volume_limit {
    name = "suppress",
    type = volume_limit.class,
    limit = -20;
    node_type = { node.phone, node.navigator },
    calculate = builtin.method.volume_supress
}
