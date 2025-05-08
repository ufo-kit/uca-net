project('ucanet', 'c')

uca_dep = dependency('libuca', version: '>= 2.0')
gio_dep = dependency('gio-2.0', version: '>= 2.22')
zmq_dep = dependency('libzmq', required: false)
json_dep = dependency('json-glib-1.0', version: '>=1.1.0', required: false)

plugindir = uca_dep.get_pkgconfig_variable('plugindir')

num_derived_classes = get_option('num_derived_net_cameras')

config = configuration_data()
config.set('HAVE_UNIX', host_machine.system() != 'windows')
if zmq_dep.found() and json_dep.found()
  config.set('WITH_ZMQ_NETWORKING', true)
endif

min_derived_classes = get_option('min_derived_net_cameras')

config.set('UCA_NET_MIN_DERIVED_CAMERAS', min_derived_classes)
config.set_quoted('UCA_NET_PLUGIN_DIR', plugindir)
config.set('UCA_NET_DEFAULT_PORT', get_option('default_port'))
config.set('UCA_NET_NUM_DERIVED_CAMERAS', num_derived_classes)

python = find_program('python3')
generator = files('generate.py')

names = []
foreach i : range(num_derived_classes)
    names += ['@0@'.format(i)]
endforeach

# Generate source files from template
foreach name : names
    input_header_name = 'uca-net-template-camera.h.in'
    input_source_name = 'uca-net-template-camera.c.in'
    input_header_abs = meson.current_source_dir() / input_header_name
    input_source_abs = meson.current_source_dir() / input_source_name

    output_header_name = 'uca-net-' + name + 'camera.h'
    output_source_name = 'uca-net-' + name + 'camera.c'
    output_header_abs = meson.current_build_dir() / output_header_name
    output_source_abs = meson.current_build_dir() / output_source_name

    run_command(python, generator, input_header_abs, output_header_abs, name,
        check: false)
    
    run_command(python, generator, input_source_abs, output_source_abs, name,
        check: false)
endforeach

input_header_name = 'uca-net-template-camera.h.in'
input_source_name = 'uca-net-template-camera.c.in'
input_header_abs = meson.current_source_dir() / input_header_name
input_source_abs = meson.current_source_dir() / input_source_name

output_header_name = 'uca-net-camera.h'
output_source_name = 'uca-net-camera.c'
output_header_abs = meson.current_build_dir() / output_header_name
output_source_abs = meson.current_build_dir() / output_source_name

run_command(python, generator, input_header_abs, output_header_abs, name,
            check: false)

run_command(python, generator, input_source_abs, output_source_abs, name,
            check: false)

configure_file(
    input: 'meson_config.h.in',
    output: 'config.h',
    configuration: config,
    install: true,
    install_dir: plugindir
)


executable('ucad',
    sources: ['ucad.c'],
    dependencies: [uca_dep, gio_dep, json_dep, zmq_dep],
    install: true,
)

# Build shared libraries
foreach name : names
    message('Building ' + name)
    shared_library('ucanet' + name,
        sources: [meson.current_build_dir() + '/' + 'uca-net-@0@camera.c'.format(name), 'uca-net-base-camera.c'],
        dependencies: [uca_dep, gio_dep],
        install: true,
        install_dir: plugindir,
    )
endforeach

message('Building ' + name)
shared_library('ucanet',
               sources: [meson.current_build_dir() + '/' + 'uca-net-camera.c', 'uca-net-base-camera.c'],
               dependencies: [uca_dep, gio_dep],
               install: true,
               install_dir: plugindir,
)
