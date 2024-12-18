project('ucanet', 'c')

uca_dep = dependency('libuca', version: '>= 2.0')
gio_dep = dependency('gio-2.0', version: '>= 2.22')
zmq_dep = dependency('libzmq', required: false)
json_dep = dependency('json-glib-1.0', version: '>=1.1.0', required: false)

plugindir = uca_dep.get_pkgconfig_variable('plugindir')

config = configuration_data()
config.set('UCA_NET_DEFAULT_PORT', get_option('default_port'))
config.set('HAVE_UNIX', host_machine.system() != 'windows')
if zmq_dep.found() and json_dep.found()
  config.set('WITH_ZMQ_NETWORKING', true)
endif

python = find_program('python3')
fs = import('fs')

### Generate source files from template
# Find all .so files in the directory
so_files = run_command('sh', '-c', 
    'cd ' + plugindir + ' && ls libuca*.so', 
    check: true).stdout().strip().split('\n')

# Extract names from libuca*.so
names = []
foreach file : so_files
    if file.startswith('libuca') and file.endswith('.so') and 'net' not in file
        name = file.split('libuca')[1].split('.so')[0]
        names += [name]
        message('Found ' + name)
    endif
endforeach

# Generate source files from template
generated_sources = []
foreach name : names
    input_header_name = 'uca-net-template-camera.h.in'
    input_source_name = 'uca-net-template-camera.c.in'
    input_header_abs = meson.current_source_dir() / input_header_name
    input_source_abs = meson.current_source_dir() / input_source_name

    output_header_name = 'uca-net-' + name + '-camera.h'
    output_source_name = 'uca-net-' + name + '-camera.c'
    output_header_abs = meson.current_source_dir() / output_header_name
    output_source_abs = meson.current_source_dir() / output_source_name

    generator = files('generate.py')

    run_command(python, generator, input_header_abs, output_header_abs, name,
        check: false)
    
    run_command(python, generator, input_source_abs, output_source_abs, name,
        check: false)

endforeach

configure_file(
    output: 'config.h',
    configuration: config,
)

shared_library('ucanet',
    sources: ['uca-net-camera.c'],
    dependencies: [uca_dep, gio_dep],
    install: true,
    install_dir: plugindir,
)

shared_library('ucanetderived',
    sources: ['uca-net-camera.c', 'uca-net-derived-camera.c'],
    dependencies: [uca_dep, gio_dep],
    install: true,
    install_dir: plugindir,
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
        sources: ['uca-net-camera.c', 'uca-net-@0@-camera.c'.format(name)],
        dependencies: [uca_dep, gio_dep],
        install: true,
        install_dir: plugindir,
    )
endforeach