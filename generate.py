import os
import sys

def generate_from_template(template_file, output_file, name):
    with open(template_file, 'r') as f:
        template = f.read()
    
    # Create case variations
    replacements = {
        '{DERIVED}': name.upper(),
        '{Derived}': name.capitalize(),
        '{derived}': name.lower()
    }
    
    # Single pass replacement
    content = template
    for pattern, replacement in replacements.items():
        content = content.replace(pattern, replacement)
    
    print (output_file)
    with open(output_file, 'w') as f:
        f.write(content)

if __name__ == '__main__':
    sys.stderr.write("yo\n")
    
    template_file = sys.argv[1]
    output_file = sys.argv[2]
    name = sys.argv[3]
    sys.stderr.write("{}, {}, {}\n".format(template_file, output_file, name))
    generate_from_template(template_file, output_file, name)