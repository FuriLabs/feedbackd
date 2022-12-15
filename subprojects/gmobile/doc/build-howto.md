Title: Compiling with gmobile
Slug: building

# Compiling with gmobile

If you need to build gmobile, get the source from
[here](https://gitlab.gnome.org/guidog/gmobile/) and see the `README.md` file.

## Bundling the library

gmobile is currently not meant to be used as a shared library. It should be
embedded in your source tree as a git submodule instead:

```
git submodule add https://gitlab.gnome.org/guidog/gmobile.git subprojects/gmobile
```

Add this to your `meson.build`:

```meson
gmobile = subproject('gmobile',
  default_options: [
    'package_name=' + meson.project_name(),
    'package_version=' + meson.project_version(),
    'pkgdatadir=' + pkgdatadir,
    'pkglibdir=' + pkglibdir,
    'examples=false',
    'gtk_doc=false',
    'tests=false',
  ])
gmobile_dep = gmobile.get_variable('gmobile_dep')
```
