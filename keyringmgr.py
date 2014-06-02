import os, subprocess
from sys import path
full_path = os.path.realpath(__file__)
base_dir = os.path.dirname(full_path)
path.append(base_dir)
os.chdir(base_dir)

import sqlite3, gnupg
from getpass import getpass

db_name="secrets.dat"
key_code = "88107DC5"

class Record(object):

    def __init__(self, db_file):
        self.conn = sqlite3.connect(db_file)
        self.cur = self.conn.cursor()

        create_table_stmt = '''
            CREATE TABLE IF NOT EXISTS accounts (
            acc_id TEXT,
            acc_pass BLOB
        );'''

        create_index = 'CREATE INDEX IF NOT EXISTS idx_id ON accounts (acc_id);'

        self.cur.execute(create_table_stmt)
        self.cur.execute(create_index)

    def get_password(self, acc_id):
        select_entry = 'SELECT * FROM accounts WHERE acc_id = ?'
        self.cur.execute(select_entry, (acc_id,))
        ret = self.cur.fetchone() 
        if ret is not None:
            return ret[1] # Return the path
        else:
            return None # Nothing found, fresh news

    def add_entry(self, acc_id, acc_pass):
        insert_entry = 'INSERT INTO accounts VALUES(?, ?)'
        self.cur.execute(insert_entry, (acc_id, sqlite3.Binary(acc_pass)))
        self.conn.commit()

    def del_entry(self, acc_id):
        del_ent = 'DELETE FROM accounts WHERE acc_id = ?' 
        self.cur.execute(del_ent, (acc_id,))
        self.conn.commit()

class Cipher(object):
    def __init__(self, key_code, gpg_pass):
        self.gpg_pass = gpg_pass
        self.key_code = key_code
        self.gpg = gnupg.GPG()

    def encrypt(self, data):
        return self.gpg.encrypt(data, key_code, armor=False).data

    def decrypt(self, data):
        return self.gpg.decrypt(data, passphrase = self.gpg_pass)

def get_plain_password(rec, cipher, acc_id):
    return cipher.decrypt(str(rec.get_password(acc_id))).data

def password(acc_id):
    return get_plain_password(Record(db_name), Cipher(key_code, getpass()), acc_id)
if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description = "Password Managing Program")
    ap.add_argument('--add', '-a', action='store')
    ap.add_argument('--delete', '-d', action='store')
    ap.add_argument('--get', '-g', action='store')
    ap.add_argument('--stdin', '-s', action='store_true')
    parse_ret = ap.parse_args()
    
    rec = Record(db_name)
    if parse_ret.stdin:
        from sys import stdin
        getpass = lambda: stdin.readline()[:-1]
    if parse_ret.add:
        cipher = Cipher(key_code, getpass())
        new_acc = parse_ret.add
        new_pass = getpass()
        rec.add_entry(new_acc, cipher.encrypt(new_pass))

    elif parse_ret.get:
        cipher = Cipher(key_code, getpass())
        print get_plain_password(rec, cipher, parse_ret.get)

    elif parse_ret.delete:
        rec.del_entry(parse_ret.delete)
