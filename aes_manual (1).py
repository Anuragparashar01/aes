# ---------- S-Box, Inv S-Box, RCON ----------
s_box = bytes.fromhex('''
63 7c 77 7b f2 6b 6f c5 30 01 67 2b fe d7 ab 76
ca 82 c9 7d fa 59 47 f0 ad d4 a2 af 9c a4 72 c0
b7 fd 93 26 36 3f f7 cc 34 a5 e5 f1 71 d8 31 15
04 c7 23 c3 18 96 05 9a 07 12 80 e2 eb 27 b2 75
09 83 2c 1a 1b 6e 5a a0 52 3b d6 b3 29 e3 2f 84
53 d1 00 ed 20 fc b1 5b 6a cb be 39 4a 4c 58 cf
d0 ef aa fb 43 4d 33 85 45 f9 02 7f 50 3c 9f a8
51 a3 40 8f 92 9d 38 f5 bc b6 da 21 10 ff f3 d2
cd 0c 13 ec 5f 97 44 17 c4 a7 7e 3d 64 5d 19 73
60 81 4f dc 22 2a 90 88 46 ee b8 14 de 5e 0b db
e0 32 3a 0a 49 06 24 5c c2 d3 ac 62 91 95 e4 79
e7 c8 37 6d 8d d5 4e a9 6c 56 f4 ea 65 7a ae 08
ba 78 25 2e 1c a6 b4 c6 e8 dd 74 1f 4b bd 8b 8a
70 3e b5 66 48 03 f6 0e 61 35 57 b9 86 c1 1d 9e
e1 f8 98 11 69 d9 8e 94 9b 1e 87 e9 ce 55 28 df
8c a1 89 0d bf e6 42 68 41 99 2d 0f b0 54 bb 16
'''.replace('\n','').replace(' ',''))
inv_s_box = bytes.fromhex('''
52 09 6a d5 30 36 a5 38 bf 40 a3 9e 81 f3 d7 fb
7c e3 39 82 9b 2f ff 87 34 8e 43 44 c4 de e9 cb
54 7b 94 32 a6 c2 23 3d ee 4c 95 0b 42 fa c3 4e
08 2e a1 66 28 d9 24 b2 76 5b a2 49 6d 8b d1 25
72 f8 f6 64 86 68 98 16 d4 a4 5c cc 5d 65 b6 92
6c 70 48 50 fd ed b9 da 5e 15 46 57 a7 8d 9d 84
90 d8 ab 00 8c bc d3 0a f7 e4 58 05 b8 b3 45 06
d0 2c 1e 8f ca 3f 0f 02 c1 af bd 03 01 13 8a 6b
3a 91 11 41 4f 67 dc ea 97 f2 cf ce f0 b4 e6 73
96 ac 74 22 e7 ad 35 85 e2 f9 37 e8 1c 75 df 6e
47 f1 1a 71 1d 29 c5 89 6f b7 62 0e aa 18 be 1b
fc 56 3e 4b c6 d2 79 20 9a db c0 fe 78 cd 5a f4
1f dd a8 33 88 07 c7 31 b1 12 10 59 27 80 ec 5f
60 51 7f a9 19 b5 4a 0d 2d e5 7a 9f 93 c9 9c ef
a0 e0 3b 4d ae 2a f5 b0 c8 eb bb 3c 83 53 99 61
17 2b 04 7e ba 77 d6 26 e1 69 14 63 55 21 0c 7d
'''.replace('\n','').replace(' ',''))
rcon = bytes.fromhex('01020408102040801b36')

# ---------- Helper ----------
def xor_bytes(a,b): return bytes(i^j for i,j in zip(a,b))
def sub_word(w): return bytes(s_box[b] for b in w)
def rot_word(w): return w[1:]+w[:1]
def mul(a,b):
 p=0
 for _ in range(8):
  if b&1: p^=a
  hi=a&0x80
  a=(a<<1)&0xFF
  if hi: a^=0x1b
  b>>=1
 return p

def pkcs7_pad(data, block=16):
 pad = block - len(data)%block
 return data + bytes([pad]*pad)

def pkcs7_unpad(data):
 pad = data[-1]
 if pad < 1 or pad > 16 or data[-pad:] != bytes([pad]*pad):
  raise ValueError("Bad padding")
 return data[:-pad]

def conditional_pad(data):
 return data if len(data)%16==0 else pkcs7_pad(data)

# ---------- AES core ----------
def bytes_to_state(b):
 return [[b[r+4*c] for c in range(4)] for r in range(4)]
def state_to_bytes(s):
 return bytes([s[r][c] for c in range(4) for r in range(4)])

def key_expansion(key):
 nk=len(key)//4; nr={16:10,24:12,32:14}[len(key)]
 w=[list(key[4*i:4*i+4]) for i in range(nk)]
 for i in range(nk,4*(nr+1)):
  temp=w[i-1][:]
  if i%nk==0: temp=list(xor_bytes(sub_word(rot_word(temp)),rcon[i//nk-1:i//nk]+b'\0\0\0'))
  elif nk>6 and i%nk==4: temp=list(sub_word(temp))
  w.append([a^b for a,b in zip(w[i-nk],temp)])
 return w

def add_round_key(s,w,r):
 for c in range(4):
  for r_ in range(4): s[r_][c]^=w[r*4+c][r_]
def sub_bytes(s):
 for r in range(4):
  for c in range(4): s[r][c]=s_box[s[r][c]]
def inv_sub_bytes(s):
 for r in range(4):
  for c in range(4): s[r][c]=inv_s_box[s[r][c]]
def shift_rows(s):
 for r in range(1,4): s[r]=s[r][r:]+s[r][:r]
def inv_shift_rows(s):
 for r in range(1,4): s[r]=s[r][-r:]+s[r][:-r]
def mix_columns(s):
 for c in range(4):
  a=[s[r][c] for r in range(4)]
  s[0][c]=mul(a[0],2)^mul(a[1],3)^a[2]^a[3]
  s[1][c]=a[0]^mul(a[1],2)^mul(a[2],3)^a[3]
  s[2][c]=a[0]^a[1]^mul(a[2],2)^mul(a[3],3)
  s[3][c]=mul(a[0],3)^a[1]^a[2]^mul(a[3],2)
def inv_mix_columns(s):
 for c in range(4):
  a=[s[r][c] for r in range(4)]
  s[0][c]=mul(a[0],14)^mul(a[1],11)^mul(a[2],13)^mul(a[3],9)
  s[1][c]=mul(a[0],9)^mul(a[1],14)^mul(a[2],11)^mul(a[3],13)
  s[2][c]=mul(a[0],13)^mul(a[1],9)^mul(a[2],14)^mul(a[3],11)
  s[3][c]=mul(a[0],11)^mul(a[1],13)^mul(a[2],9)^mul(a[3],14)

def aes_encrypt(block,key):
 s=bytes_to_state(block)
 w=key_expansion(key)
 nr={16:10,24:12,32:14}[len(key)]
 add_round_key(s,w,0)
 for r in range(1,nr):
  sub_bytes(s); shift_rows(s); mix_columns(s); add_round_key(s,w,r)
 sub_bytes(s); shift_rows(s); add_round_key(s,w,nr)
 return state_to_bytes(s)

def aes_decrypt(block,key):
 s=bytes_to_state(block)
 w=key_expansion(key)
 nr={16:10,24:12,32:14}[len(key)]
 add_round_key(s,w,nr)
 for r in range(nr-1,0,-1):
  inv_shift_rows(s); inv_sub_bytes(s); add_round_key(s,w,r); inv_mix_columns(s)
 inv_shift_rows(s); inv_sub_bytes(s); add_round_key(s,w,0)
 return state_to_bytes(s)

# ---------- Modes ----------
def ecb_enc(data,key):
 return b''.join(aes_encrypt(data[i:i+16],key) for i in range(0,len(data),16))
def ecb_dec(data,key):
 return b''.join(aes_decrypt(data[i:i+16],key) for i in range(0,len(data),16))
def cbc_enc(data,key,iv):
 out=b''
 prev=iv
 for i in range(0,len(data),16):
  b=xor_bytes(data[i:i+16],prev)
  e=aes_encrypt(b,key)
  out+=e
  prev=e
 return out
def cbc_dec(data,key,iv):
 out=b''
 prev=iv
 for i in range(0,len(data),16):
  b=data[i:i+16]
  d=aes_decrypt(b,key)
  out+=xor_bytes(d,prev)
  prev=b
 return out

# ---------- MAIN ----------
if __name__=="__main__":
 mode=input("Mode ECB/CBC: ").strip().upper()
 key_size=int(input("Key 128/192/256: "))
 key_len={128:16,192:24,256:32}[key_size]
 key=input("Key: ").encode().ljust(key_len,b'\0')[:key_len]
 pt=input("Plaintext: ").encode()
 data=conditional_pad(pt)
 if mode=="CBC":
  iv=input("IV (16 char): ").encode().ljust(16,b'\0')[:16]
  ct=cbc_enc(data,key,iv)
  dec=cbc_dec(ct,key,iv)
 else:
  ct=ecb_enc(data,key)
  dec=ecb_dec(ct,key)
 dec=pkcs7_unpad(dec) if len(pt)%16!=0 else dec
 print("Cipher:",ct.hex())
 print("Decrypted:",dec.decode(errors='ignore'))
