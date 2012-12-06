
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

