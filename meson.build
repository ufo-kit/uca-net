project('ucanet', 'c')

uca_dep = dependency('libuca', version: '>= 2.0')
gio_dep = dependency('gio-2.0', verson: '>= 2.22')

plugindir = uca_dep.get_pkgconfig_variable('plugindir')

config = configuration_data()
config.set('UCA_NET_DEFAULT_PORT', get_option('default_port'))

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
