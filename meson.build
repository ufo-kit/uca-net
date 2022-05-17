project('ucanet', 'c')

uca_dep = dependency('libuca', version: '>= 2.4.1')
gio_dep = dependency('gio-2.0', version: '>= 2.22')

plugindir = uca_dep.get_pkgconfig_variable('plugindir')

config = configuration_data()
config.set('UCA_NET_DEFAULT_PORT', get_option('default_port'))
config.set('HAVE_UNIX', host_machine.system() != 'windows')
if uca_dep.get_pkgconfig_variable('with_python_multithreading') ==  'true'
  config.set('UCA_WITH_PYTHON_MULTITHREADING', true)
endif

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

executable('ucad',
    sources: ['ucad.c'],
    dependencies: [uca_dep, gio_dep],
    install: true,
)
