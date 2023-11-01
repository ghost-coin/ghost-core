#!/usr/bin/env python3
# Copyright (c) 2017-2022 The Particl Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_particl import GhostTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_is_hex_string


class MnemonicTest(GhostTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [ ['-debug',] for i in range(self.num_nodes)]

    def setup_network(self, split=False):
        self.add_nodes(self.num_nodes, extra_args=self.extra_args)
        self.start_nodes()

        self.sync_all()

    def run_test(self):
        node = self.nodes[0]

        # mnemonic new [password] [language] [nBytesEntropy] [bip44]

        # check default key is bip44
        json_obj = node.mnemonic('new')
        key = json_obj['master']
        assert (key.startswith('tprv')), 'Key is not bip44.'

        # check bip32 key is made
        json_obj = node.mnemonic('new', '', 'english', '32', 'false')
        key = json_obj['master']
        assert (key.startswith('xpar')), 'Key is not bip32.'

        checkLangs = ['english', 'french', 'japanese', 'spanish', 'chinese_s', 'chinese_t', 'italian', 'korean', 'czech']

        for i in range(8):
            for l in checkLangs:
                json_obj = node.mnemonic('new', '', l)
                keyBip44 = json_obj['master']
                words = json_obj['mnemonic']
                assert (keyBip44.startswith('tprv')), 'Key is not bip44.'

                json_obj = node.mnemonic('decode', '', words)
                assert json_obj['master'] == keyBip44, 'Decoded bip44 key does not match.'

                # bip32
                json_obj = node.mnemonic('new', '', l, '32', 'false')
                keyBip32 = json_obj['master']
                words = json_obj['mnemonic']
                assert (keyBip32.startswith('xpar')), 'Key is not bip32.'

                json_obj = node.mnemonic('decode', '', words, 'false')
                assert json_obj['master'] == keyBip32, 'Decoded bip32 key does not match.'

                # with pwd
                json_obj = node.mnemonic('new', 'qwerty123', l)
                keyBip44Pass = json_obj['master']
                words = json_obj['mnemonic']
                assert (keyBip44Pass.startswith('tprv')), 'Key is not bip44.'

                json_obj = node.mnemonic('decode', 'qwerty123', words)
                assert json_obj['master'] == keyBip44Pass, 'Decoded bip44 key with password does not match.'

                json_obj = node.mnemonic('decode', 'wrongpass', words)
                assert json_obj['master'] != keyBip44Pass, 'Decoded bip44 key with wrong password should not match.'

        try:
            ro = node.mnemonic('new', '', 'english', '15')
            assert (False), 'Generated mnemonic from < 16bytes entropy.'
        except JSONRPCException as e:
            assert ('Num bytes entropy out of range [16,64]' in e.error['message'])

        for i in range(16, 65):
            ro = node.mnemonic('new', '', 'english', str(i))

        try:
            ro = node.mnemonic('new', '', 'english', '65')
            assert (False), 'Generated mnemonic from > 64bytes entropy .'
        except JSONRPCException as e:
            assert ('Num bytes entropy out of range [16,64]' in e.error['message'])

        ro = node.mnemonic('new', '', 'english', '64')
        assert (len(ro['mnemonic'].split(' ')) == 48)

        try:
            ro = node.mnemonic('new', '', 'abcdefgh', '15')
            assert (False), 'Generated mnemonic from unknown language.'
        except JSONRPCException as e:
            assert ('Unknown language' in e.error['message'])


        ro = node.mnemonic('dumpwords')
        assert (ro['num_words'] == 2048)

        for l in checkLangs:
            ro = node.mnemonic('dumpwords', l)
            assert (ro['num_words'] == 2048)

        ro = node.mnemonic('listlanguages')
        assert (len(ro) == 9)


        self.log.info('Test incorrect parameter order: mnemonic,password vs password,mnemonic')
        try:
            ro = node.mnemonic('decode', 'abandon baby cabbage dad eager fabric gadget habit ice kangaroo lab absorb', '')
            assert (False), 'Decoded empty word string.'
        except JSONRPCException as e:
            assert ("Mnemonic can't be blank" in e.error['message'])

        self.log.info('Normalisation')
        # Normalise 'alléger'
        ro = node.mnemonic('decode', '', 'sortir hygiène boueux détourer doyen émission prospère tunnel cerveau miracle brioche feuille arbitre terne alléger prison connoter diable méconnu fraise pelle carbone erreur admettre')
        assert (ro['master'] == 'tprv8ZgxMBicQKsPdsKV1vzsQkRQp5TobgyfXsBLcU49jmnC2zBT4Cd5LTCtdoWe5gg7EPjjQnAsxbMG1qyoCn1bHn6n4c1ZEdFLKg1TJAwTriQ')

        self.log.info('Leading and trailing spaces')
        ro = node.mnemonic('decode', '', ' sortir hygiène boueux détourer doyen émission prospère tunnel cerveau miracle brioche feuille arbitre terne alléger prison connoter diable méconnu fraise pelle carbone erreur admettre  ')
        assert (ro['master'] == 'tprv8ZgxMBicQKsPdsKV1vzsQkRQp5TobgyfXsBLcU49jmnC2zBT4Cd5LTCtdoWe5gg7EPjjQnAsxbMG1qyoCn1bHn6n4c1ZEdFLKg1TJAwTriQ')

        self.log.info('Multiple spaces between words')
        try:
            ro = node.mnemonic('decode', '', 'abandon  baby cabbage dad eager fabric gadget habit ice kangaroo lab absorb')
            assert (False), 'Decoded with multiple spaces.'
        except JSONRPCException as e:
            assert ("Multiple spaces between words" in e.error['message'])

        try:
            ro = node.mnemonic('decode', '', 'abandon  baby cabbage dad eager fabric gadget habit ice   kangaroo lab absorb')
            assert (False), 'Decoded with multiple spaces.'
        except JSONRPCException as e:
            assert ("Multiple spaces between words" in e.error['message'])

        self.log.info('Test shamir39 scheme')
        ro = node.mnemonic('listlanguages')
        assert (len(ro.keys()) == 9)
        for lang in ro.keys():
            bytes_entropy = random.randint(16, 64)
            mnemonic = node.mnemonic('new', '', lang, bytes_entropy)['mnemonic']
            num_shares = random.randint(2, 800)  # Reduced from 2047 for quicker tests
            threshold = random.randint(2, num_shares)

            shares = node.splitmnemonic({'mnemonic': mnemonic, 'numshares': num_shares, 'threshold': threshold, 'language': lang})
            assert (len(shares) == num_shares)

            # Working directly with the data lists is slow
            offsets = [*range(len(shares))]
            random.shuffle(offsets)

            use_shares = []
            for i in range(threshold):
                use_shares.append(shares[offsets[i]])

            recovered_mnemonic = node.combinemnemonic({'shares': use_shares})
            assert (mnemonic == recovered_mnemonic)

        self.log.info('Test to and from entropy')
        test_mnemonic = node.mnemonic('new')['mnemonic']
        entropy_hex = node.mnemonictoentropy({'mnemonic': test_mnemonic})
        assert_is_hex_string(entropy_hex)

        mnemonic_out = node.mnemonicfromentropy({'entropy': entropy_hex})
        assert (mnemonic_out == test_mnemonic)


if __name__ == '__main__':
    MnemonicTest().main()
