import matplotlib.pyplot as plt
import matplotlib.patches as patches
from matplotlib.patches import FancyArrowPatch
import numpy as np
import os

def generate_pipeline_diagram():
    # Set up the figure and axes
    fig, ax = plt.subplots(figsize=(20, 18))
    ax.set_xlim(0, 20)
    ax.set_ylim(0, 18)
    ax.axis('off')

    # --- PREMIUM GOLD & CHARCOAL THEME COLORS ---
    color_bg = "#0D0D0D"         # Almost Black
    color_header_footer = "#1A1A1A" # Deep Charcoal
    color_node_bg = "#222222"    # Dark Grey Nodes
    color_node_border = "#333333" # Muted Border
    color_text_primary = "#FFFFFF" # White
    color_text_secondary = "#888888" # Darker Silver
    color_gold = "#D4AF37"       # Classic Gold
    color_arrow = "#555555"      # Muted Grey Arrows
    color_gold_arrow = "#B8860B" # Darker Gold for arrows to avoid being too bright

    fig.patch.set_facecolor(color_bg)
    ax.set_facecolor(color_bg)

    # --- SUBTLE SHIMMER EFFECT ---
    np.random.seed(42)
    shimmer_x = np.random.uniform(0, 20, 3500)
    shimmer_y = np.random.uniform(0, 18, 3500)
    shimmer_alpha = np.random.uniform(0.01, 0.05, 3500)
    ax.scatter(shimmer_x, shimmer_y, s=1, c=color_gold, alpha=shimmer_alpha, edgecolors='none', zorder=0)

    # --- FULL-WIDTH HEADER & FOOTER ---
    header_rect = patches.Rectangle((0, 16.5), 20, 1.5, facecolor=color_header_footer, edgecolor='none', zorder=1)
    ax.add_patch(header_rect)
    ax.text(10, 17.3, "NEXRAD LEVEL II PIPELINE", 
            ha='center', va='center', fontweight='black', fontsize=32, color=color_gold, zorder=2)
    ax.text(10, 16.8, "HIGH-PERFORMANCE REAL-TIME PROCESSING ENGINE", 
            ha='center', va='center', fontweight='bold', fontsize=12, color=color_text_secondary, zorder=2)

    footer_rect = patches.Rectangle((0, 0), 20, 0.8, facecolor=color_header_footer, edgecolor='none', zorder=1)
    ax.add_patch(footer_rect)
    ax.text(10, 0.4, "• VERSION 1.0 •", 
            ha='center', va='center', fontweight='bold', fontsize=10, color=color_text_secondary, zorder=2)

    # Helper function to draw a sleek node
    def draw_node(x, y, width, height, label, description="", label_fs=18, desc_fs=12):
        # Premium Gold Theme for ALL nodes
        rect = patches.FancyBboxPatch((x, y), width, height, boxstyle="round,pad=0.3",
                                     facecolor=color_node_bg, edgecolor=color_gold, linewidth=1.5, zorder=3)
        ax.add_patch(rect)
        ax.text(x + width / 2, y + height / 2 + 0.4, label,
                ha='center', va='center', fontweight='bold', fontsize=label_fs, color=color_gold, zorder=4)
        if description:
            ax.text(x + width / 2, y + height / 2 - 0.4, description,
                    ha='center', va='center', fontsize=desc_fs, style='italic', color=color_text_secondary, zorder=4)

    # --- TREE LAYOUT WITH ALL NODES GOLD ---
    
    # Level 1: Root (Top Center)
    draw_node(7, 14, 6, 1.5, "NOAA S3 DATA ROOT", "unidata-nexrad-level2")

    # Level 2: Discovery Layer (Left Branch)
    draw_node(1.5, 11, 4.5, 1.5, "DISCOVERY LOOP", "Background Scan Thread")
    draw_node(2, 8.5, 3.5, 1, "STATION FILTER", "KTLX, KCRP, KEWX")

    # Level 3: Processing Layer (Worker Threads)
    draw_node(7.5, 9, 5, 3, "PIPELINE ENGINE", 
              "**Worker Threads**\nS3 Download Ingestion\nDecompression & Decoding\nRadarParser (Message 31)")
    
    # Helper: Memory Manager
    draw_node(13.5, 10.5, 4.5, 1.5, "BUFFER POOL", "Pre-allocated Memory Management")

    # Level 4: Output Layer (Bottom Distribution)
    draw_node(1.5, 3.5, 4, 1.5, "DISK STORAGE", "RadarFrame Serialization")
    draw_node(8, 3.5, 4, 1.5, "VOLUMETRIC GEN", "3D Grid Reconstruction")
    draw_node(14.5, 3.5, 4, 1.5, "ADMIN SERVER", "REST API & UI Control")

    # --- ARROWS ---
    def draw_pro_arrow(start, end, rad=0.0):
        # Using a darker gold for arrows to provide a sophisticated transition
        arrow = FancyArrowPatch(start, end, connectionstyle=f"arc3,rad={rad}",
                                arrowstyle='->', mutation_scale=20, 
                                color=color_gold_arrow, linewidth=2,
                                shrinkA=15, shrinkB=15, zorder=2)
        ax.add_patch(arrow)

    # Root -> Discovery
    draw_pro_arrow((7.5, 14.5), (5, 12), rad=0.1)
    # Root -> Pipeline (Direct Down)
    draw_pro_arrow((10, 14), (10, 12.5), rad=0)

    # Discovery -> Filter
    draw_pro_arrow((3.75, 11), (3.75, 10), rad=0)
    # Filter -> Pipeline
    draw_pro_arrow((5.5, 9.5), (7.5, 9.5), rad=0)

    # Pipeline <-> Buffer Pool
    draw_pro_arrow((12.5, 11.5), (13.5, 11.25), rad=0.2)
    draw_pro_arrow((13.5, 10.25), (12.5, 10), rad=0.2)

    # Pipeline Engine -> Outputs
    draw_pro_arrow((8, 9), (4, 5.5), rad=0.1)
    draw_pro_arrow((10, 9), (10, 5.5), rad=0)
    draw_pro_arrow((12, 9), (16, 5.5), rad=-0.1)

    # Save
    output_path = "docs/images/pipeline_diagram.png"
    plt.savefig(output_path, dpi=150, bbox_inches='tight', pad_inches=0, facecolor=fig.get_facecolor())
    print(f"Premium all-gold-themed diagram generated: {output_path}")

if __name__ == "__main__":
    generate_pipeline_diagram()
