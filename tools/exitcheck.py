"""Place the Prince in every level-exit Area (K2_EXITAUDIT lists them) and check the level ends."""
import subprocess,re,os,sys
os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
rooms=sorted(os.listdir('data/geometry/rooms'))
env=dict(os.environ,K2_EXITAUDIT='1')
def run(n,extra,e=env):
    return subprocess.run(['./build/k2boy','--menu-game','--level','geometry\\rooms\\%s\\%s.lvl'%(n,n),'--fixeddt','0.0333','--shot','/tmp/k2boy_exitcheck.bmp','data']+extra,env=e,capture_output=True,text=True,timeout=300)
for n in rooms:
    o=run(n,['--frames','1']).stderr
    for m in re.finditer(r'exitaudit (\w+) (.+?) box (\S+),(\S+),(\S+) \.\. (\S+),(\S+),(\S+): (.*)',o):
        t,name,x0,y0,z0,x1,y1,z1,msg=m.groups()
        if t!='Area' or 'game,exit' not in msg.lower() or 'exit:lose' in msg.lower(): continue
        x=(float(x0)+float(x1))/2; z=(float(z0)+float(z1))/2; y=float(y0)+0.6
        r=run(n,['--place','%.2f,%.2f,%.2f,0'%(x,y,z),'--frames','240'],os.environ)
        out=r.stdout+r.stderr
        hit=re.search(r'level: [^\n]*',out)
        print('%-14s %-28s %s'%(n,name,hit.group(0) if hit else 'NO EXIT'),flush=True)
