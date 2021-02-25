with open('static.conf', 'w') as f:
    for i in range(8, 16):
        for j in range(0, 256):
            f.write(f'route 10.{i}.{j}.0/24 via "lo";\n')
