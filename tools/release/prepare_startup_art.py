#!/usr/bin/env python3
"""Prepare locally owned startup artwork; generated retail assets stay out of Git."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--loading-image', required=True, type=Path)
    parser.add_argument('--ring-movie', required=True, type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'godot/startup_art')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    info = json.loads(subprocess.check_output([
        'ffprobe', '-v', 'error', '-show_streams', '-of', 'json', str(args.ring_movie)]))
    stream = info['streams'][0]
    frames = int(stream['nb_frames'])
    numerator, denominator = map(int, stream['r_frame_rate'].split('/'))
    def convert(source, options, target):
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(source),
                        *options, '-frames:v', '1', str(args.output / target)], check=True)
    convert(args.loading_image, ['-vf', 'crop=iw:iw*3/4:0:0'], 'loading.png')
    convert(args.ring_movie, ['-filter_complex',
            f'[0:v:0][0:v:1]alphamerge,tile=10x{(frames + 9) // 10}'], 'ring_atlas.png')
    convert(args.ring_movie, ['-filter_complex', '[0:v:0][0:v:1]alphamerge'], 'ring_first.png')
    (args.output / 'animation.json').write_text(json.dumps({
        'frames': frames, 'columns': 10, 'width': stream['width'],
        'height': stream['height'], 'fps': numerator / denominator}, indent=2) + '\n')


if __name__ == '__main__':
    main()
