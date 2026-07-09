# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

project = 'STEAM Development Kit Documentation!'
copyright = '2026 Tu Dang'
author = 'Tu Dang'
release = '0.1'


# -- General configuration ---------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration

extensions = ['myst_parser',"sphinx_design"]

templates_path = ['_templates']

exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']

html_static_path = ['_static']
# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

html_theme = 'conestack'

html_theme_options = {
    'logo_url': '_static/logo-removebg.png',
    'logo_title': 'The STEAM Development Kit',
    'logo_width': '40px',
    'logo_height': '20px'
}
# html_static_path = ['_static']
# html_theme_options = {
#     # Ensure this is set to False (or omitted) so the right-side TOC remains visible
#     'hide_localtoc': False,
    
#     # Optional: Ensures your global multi-page structure shows up on the left sidebar
#     'hide_globaltoc': False,
# }