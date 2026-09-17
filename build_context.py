import os
import sys
import xml.etree.ElementTree as ET

def generate_gemini_context(root_dir, output_file="gemini_context.xml"):
    """
    Traverses a local directory, builds a visual text tree, and packs 
    all valid file contents into an XML layout optimized for Gemini 1.5 Pro.
    """
    if not os.path.exists(root_dir):
        print(f"Error: Directory '{root_dir}' does not exist.")
        return

    # Text extensions safely parsed as code context
    TEXT_EXTENSIONS = {
        '.txt', '.md', '.json', '.xml', '.sln', '.cs', '.py', '.js', 
        '.ts', '.html', '.css', '.cpp', '.h', '.sh', '.bat', '.ini', '.yaml', '.yml'
    }
    
    # Folders to completely skip to avoid bloating token limit
    IGNORE_FOLDERS = {'.git', '.github', 'bin', 'obj', 'packages', 'node_modules', '__pycache__'}

    print(f"Scanning directory: {root_dir}")
    
    # 1. Build the text-based Directory Tree
    tree_lines = []
    def build_tree(path, prefix=""):
        try:
            items = sorted(os.listdir(path))
        except PermissionError:
            return
            
        items = [i for i in items if i not in IGNORE_FOLDERS]
        for i, item in enumerate(items):
            is_last = (i == len(items) - 1)
            connector = "└── " if is_last else "├── "
            full_path = os.path.join(path, item)
            
            tree_lines.append(f"{prefix}{connector}{item}")
            
            if os.path.isdir(full_path):
                next_prefix = prefix + ("    " if is_last else "│   ")
                build_tree(full_path, next_prefix)

    root_name = os.path.basename(os.path.abspath(root_dir))
    tree_lines.append(f"└── {root_name}/")
    build_tree(root_dir, "    ")
    directory_tree_string = "\n".join(tree_lines)

    # 2. Gather File Content Payloads
    files_payload_buffer = []
    
    for current_root, dirs, files in os.walk(root_dir):
        # In-place filtering to skip ignored directories
        dirs[:] = [d for d in dirs if d not in IGNORE_FOLDERS]
        
        for file in files:
            file_path = os.path.join(current_root, file)
            rel_path = os.path.relpath(file_path, root_dir)
            _, ext = os.path.splitext(file.lower())
            
            if ext in TEXT_EXTENSIONS:
                try:
                    with open(file_path, 'r', encoding='utf-8', errors='replace') as f:
                        content = f.read()
                    
                    # Wrap file payload using standard string blocks to prevent parsing collision
                    file_block = f'<file path="{rel_path}">\n{content}\n</file>'
                    files_payload_buffer.append(file_block)
                except Exception as e:
                    print(f"Skipping file {rel_path} due to read error: {e}")

    # 3. Assemble the Final Structured XML Prompt File
    with open(output_file, 'w', encoding='utf-8') as out:
        out.write("<system_instructions>\n")
        out.write("You are an expert software engineer analyzing the provided codebase.\n")
        out.write("Review the directory structure first, then read individual file contents mapped to their paths.\n")
        out.write("Do not reply until you have read the task constraints at the absolute end of this prompt.\n")
        out.write("</system_instructions>\n\n")
        
        out.write("<codebase_context>\n\n")
        
        out.write("<directory_tree>\n")
        out.write(directory_tree_string + "\n")
        out.write("</directory_tree>\n\n")
        
        out.write("<files_payload>\n\n")
        out.write("\n\n".join(files_payload_buffer) + "\n\n")
        out.write("</files_payload>\n\n")
        
        out.write("</codebase_context>\n\n")
        
        out.write("<task_instructions>\n")
        out.write("TASK: [Insert your prompt query, analysis, refactoring or debugging goal here]\n")
        out.write("FORMAT: Provide structured explanations alongside final, clean code snippets.\n")
        out.write("</task_instructions>\n")

    print(f"Successfully generated structured XML context file at: {output_file}")

if __name__ == "__main__":
    target = sys.argv[1] if len(sys.argv) > 1 else "."
    generate_gemini_context(target)
