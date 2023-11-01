#!/usr/bin/env python3
# Copyright (c) 2017-2021 The Particl Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from decimal import Decimal

from test_framework.test_particl import GhostTestFramework
from test_framework.util import assert_equal
from test_framework.address import keyhash_to_p2pkh
from test_framework.authproxy import JSONRPCException


def keyhash_to_p2pkh_part(b):
    return keyhash_to_p2pkh(b, False, False)


class ColdStakingTest(GhostTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [ ['-debug','-reservebalance=10000000', '-anonrestricted=0'] for i in range(self.num_nodes)]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self, split=False):
        self.add_nodes(self.num_nodes, extra_args=self.extra_args)
        self.start_nodes()

        self.connect_nodes_bi(0, 1)
        self.connect_nodes_bi(0, 2)
        self.sync_all()

    def run_test(self):
        nodes = self.nodes

        self.import_genesis_coins_a(nodes[0])

        nodes[1].extkeyimportmaster('drip fog service village program equip minute dentist series hawk crop sphere olympic lazy garbage segment fox library good alley steak jazz force inmate')
        nodes[2].extkeyimportmaster('sección grito médula hecho pauta posada nueve ebrio bruto buceo baúl mitad')

        addr2_1 = nodes[2].getnewaddress()


        ro = nodes[1].extkey('account')
        coldstakingaddr = ''
        for c in ro['chains']:
            if c['function'] != 'active_external':
                continue
            coldstakingaddr = c['chain']
            break
        assert(coldstakingaddr == 'pparszNYZ1cpWxnNiYLgR193XoZMaJBXDkwyeQeQvThTJKjz3sgbR4NjJT3bqAiHBk7Bd5PBRzEqMiHvma9BG6i9qH2iEf4BgYvfr5v3DaXEayNE')
        coldstakingaddr_ext = coldstakingaddr

        changeaddress = {'coldstakingaddress': coldstakingaddr}
        ro = nodes[0].walletsettings('changeaddress', changeaddress)
        assert (ro['changeaddress']['coldstakingaddress'] == coldstakingaddr)

        ro = nodes[0].walletsettings('changeaddress')
        assert (ro['changeaddress']['coldstakingaddress'] == coldstakingaddr)

        ro = nodes[0].walletsettings('changeaddress', {})
        assert (ro['changeaddress'] == 'cleared')

        ro = nodes[0].walletsettings('changeaddress')
        assert (ro['changeaddress'] == 'default')

        ro = nodes[0].walletsettings('changeaddress', changeaddress)
        assert (ro['changeaddress']['coldstakingaddress'] == coldstakingaddr)


        # Trying to set a coldstakingchangeaddress known to the wallet should fail
        ro = nodes[0].extkey('account')
        externalChain0 = ''
        for c in ro['chains']:
            if c['function'] != 'active_external':
                continue
            externalChain0 = c['chain']
            break
        assert (externalChain0 == 'pparszMzzW1247AwkKCH1MqneucXJfDoR3M5KoLsJZJpHkcjayf1xUMwPoTcTfUoQ32ahnkHhjvD2vNiHN5dHL6zmx8vR799JxgCw95APdkwuGm1')

        changeaddress = {'coldstakingaddress': externalChain0}
        try:
            ro = nodes[0].walletsettings('changeaddress', changeaddress)
            assert (False), 'Added known address as cold-staking-change-address.'
        except JSONRPCException as e:
            assert ('is spendable from this wallet' in e.error['message'])

        assert_equal(nodes[0].getcoldstakinginfo()['coin_in_coldstakeable_script'], Decimal(0))
        txid1 = nodes[0].sendtoaddress(addr2_1, 100)
        tx = nodes[0].getrawtransaction(txid1, True)

        cs_info = nodes[0].getcoldstakinginfo()
        assert_equal(cs_info['coin_in_coldstakeable_script'], Decimal('9899.999572'))
        assert_equal(cs_info['coin_in_stakeable_script'], cs_info['currently_staking'] + cs_info['pending_depth'])

        hashCoinstake = ''
        hashOther = ''
        found = False
        for out in tx['vout']:
            asm = out['scriptPubKey']['asm']
            asm = asm.split()
            if asm[0] != 'OP_ISCOINSTAKE':
                continue
            hashCoinstake = asm[4]
            hashOther = asm[10]

        assert (hashCoinstake == '65674e752b3a336337510bf5b57794c71c45cd4f')
        assert (hashOther == 'e5c8967e77fdeecaa46a446a0f71988c65b51432f35f8e58fdfe628c5a169386')

        ro = nodes[0].deriverangekeys(0, 0, coldstakingaddr)
        assert (ro[0] == keyhash_to_p2pkh_part(bytes.fromhex(hashCoinstake)))


        ro = nodes[0].extkey('list', 'true')

        fFound = False
        for ek in ro:
            if ek['id'] == 'xBDBWFLeYrbBhPRSKHzVwN61rwUGwCXvUB':
                fFound = True
                assert (ek['evkey'] == 'Unknown')
                assert (ek['num_derives'] == '1')
        assert (fFound)

        assert (self.wait_for_mempool(nodes[1], txid1))

        ro = nodes[1].extkey('key', 'xBDBWFLeYrbBhPRSKHzVwN61rwUGwCXvUB', 'true')
        assert (ro['num_derives'] == '1')

        ro = nodes[1].listtransactions('*', 999999, 0, True)
        assert (len(ro) == 1)

        ro = nodes[1].getwalletinfo()
        last_balance = ro['watchonly_unconfirmed_balance']
        assert (last_balance > 0)

        ekChange = nodes[0].getnewextaddress()
        assert (ekChange == 'pparszMzzW1247AwkR61QFUH6L8zSJDnRvsS8a2FLwfSsgbeusiLNdBkLRXjFb3E5AXVoR6PJTj9nSEF1feCsCyBdGw165XqVcaWs5HiDmcZrLAX')

        changeaddress = {'coldstakingaddress': coldstakingaddr, 'address_standard': ekChange}
        ro = nodes[0].walletsettings('changeaddress', changeaddress)
        assert (ro['changeaddress']['coldstakingaddress'] == coldstakingaddr)
        assert (ro['changeaddress']['address_standard'] == ekChange)

        txid2 = nodes[0].sendtoaddress(addr2_1, 100)

        tx = nodes[0].getrawtransaction(txid2, True)

        hashCoinstake = ''
        hashSpend = ''
        found = False
        for out in tx['vout']:
            asm = out['scriptPubKey']['asm']
            asm = asm.split()
            if asm[0] != 'OP_ISCOINSTAKE':
                continue
            hashCoinstake = asm[4]
            hashSpend = asm[10]

        assert (hashCoinstake == '1ac277619e43a7e0558c612f86b918104742f65c')
        assert (hashSpend == '55e9e9b1aebf76f2a2ce9d7af6267be996bc235e3a65fa0f87a345267f9b3895')

        ro = nodes[0].deriverangekeys(1, 1, coldstakingaddr)
        assert (ro[0] == keyhash_to_p2pkh_part(bytes.fromhex(hashCoinstake)))

        ro = nodes[0].deriverangekeys(0, 0, ekChange, False, False, False, True)
        assert (ro[0] == keyhash_to_p2pkh_part(bytes.fromhex(hashSpend)))

        ro = nodes[0].extkey('list', 'true')
        fFound = False
        for ek in ro:
            if ek['id'] == 'xBDBWFLeYrbBhPRSKHzVwN61rwUGwCXvUB':
                fFound = True
                assert (ek['evkey'] == 'Unknown')
                assert (ek['num_derives'] == '2')
        assert (fFound)

        ro = nodes[0].extkey('account')
        fFound = False
        for chain in ro['chains']:
            if chain['id'] == 'xXZRLYvJgbJyrqJhgNzMjEvVGViCdGmVAt':
                fFound = True
                assert (chain['num_derives'] == '1')
                assert (chain['path'] == 'm/0h/2')
        assert (fFound)

        assert (self.wait_for_mempool(nodes[1], txid2))

        ro = nodes[1].extkey('key', 'xBDBWFLeYrbBhPRSKHzVwN61rwUGwCXvUB', 'true')
        assert (ro['num_derives'] == '2')

        ro = nodes[1].listtransactions('*', 999999, 0, True)
        assert (len(ro) == 2)

        ro = nodes[1].getwalletinfo()
        assert (ro['watchonly_unconfirmed_balance'] > last_balance)

        txid3 = nodes[0].sendtoaddress(addr2_1, 100)
        tx = nodes[0].getrawtransaction(txid3, True)

        hashCoinstake = ''
        hashSpend = ''
        found = False
        for out in tx['vout']:
            asm = out['scriptPubKey']['asm']
            asm = asm.split()
            if asm[0] != 'OP_ISCOINSTAKE':
                continue
            hashCoinstake = asm[4]
            hashSpend = asm[10]

        ro = nodes[0].deriverangekeys(2, 2, coldstakingaddr)
        assert (ro[0] == keyhash_to_p2pkh_part(bytes.fromhex(hashCoinstake)))

        ro = nodes[0].deriverangekeys(1, 1, ekChange, False, False, False, True)
        assert (ro[0] == keyhash_to_p2pkh_part(bytes.fromhex(hashSpend)))

        ro = nodes[0].extkey('list', 'true')
        fFound = False
        for ek in ro:
            if ek['id'] == 'xBDBWFLeYrbBhPRSKHzVwN61rwUGwCXvUB':
                fFound = True
                assert (ek['evkey'] == 'Unknown')
                assert (ek['num_derives'] == '3')
        assert (fFound)

        ro = nodes[0].extkey('account')
        fFound = False
        for chain in ro['chains']:
            if chain['id'] == 'xXZRLYvJgbJyrqJhgNzMjEvVGViCdGmVAt':
                fFound = True
                assert (chain['num_derives'] == '2')
                assert (chain['path'] == 'm/0h/2')
        assert (fFound)

        assert(self.wait_for_mempool(nodes[1], txid3))
        assert(nodes[1].extkey('key', 'xBDBWFLeYrbBhPRSKHzVwN61rwUGwCXvUB', 'true')['num_derives'] == '3')

        # Test stake to coldstakingchangeaddress
        nodes[0].walletsettings('stakelimit', {'height': 2})
        nodes[0].reservebalance(False)

        # Test walletsettings stakelimit view path
        assert (nodes[0].walletsettings('stakelimit')['height'] == 2)

        assert (self.wait_for_height(nodes[0], 2))
        self.sync_all()

        assert (nodes[1].getwalletinfo()['watchonly_staked_balance'] > 0)

        ro = nodes[0].extkey('list', 'true')
        fFound = False
        for ek in ro:
            if ek['id'] == 'xBDBWFLeYrbBhPRSKHzVwN61rwUGwCXvUB':
                fFound = True
                assert (ek['evkey'] == 'Unknown')
                assert (ek['num_derives'] == '5')
        assert (fFound)

        # Test mapRecord watchonly
        wotBefore = nodes[1].getwalletinfo()['watchonly_total_balance']

        n1unspent = nodes[1].listunspent()
        addr2_1s = nodes[2].getnewstealthaddress()

        coincontrol = {'inputs': [{'tx': n1unspent[0]['txid'],'n': n1unspent[0]['vout']}]}
        outputs = [{'address': addr2_1s, 'amount': 1, 'narr': 'p2b,0->2'},]
        txid = nodes[0].sendtypeto('part', 'blind', outputs, 'comment', 'comment-to', 4, 64, False, coincontrol)

        self.sync_all()

        wotAfter = nodes[1].getwalletinfo()['watchonly_total_balance']
        assert (wotAfter > wotBefore-Decimal(2.0))

        assert (len(nodes[1].listtransactions('*', 10, 0)) == 0)

        txn_list = nodes[1].listtransactions('*', 10, 0, True)

        fFound = False
        for txn in txn_list:
            if txn['txid'] == txid:
                fFound = True
                assert (txn['involvesWatchonly'] == True)
        assert (fFound)

        self.log.info('Test gettxoutsetinfobyscript')
        ro = nodes[0].gettxoutsetinfobyscript()
        assert (ro['coldstake_paytopubkeyhash']['num_plain'] > 5)

        self.log.info('Test p2sh in changeaddress')
        ms_addrs0 = []
        ms_pubkeys0 = []
        ms_addrs1 = []
        ms_pubkeys1 = []

        ms_addrs0.append(nodes[0].getnewaddress())
        ms_addrs0.append(nodes[1].getnewaddress())
        ms_pubkeys0.append(nodes[0].getaddressinfo(ms_addrs0[0])['pubkey'])
        ms_pubkeys0.append(nodes[1].getaddressinfo(ms_addrs0[1])['pubkey'])

        ms_addr0 = nodes[0].addmultisigaddress_part(1, ms_pubkeys0)  # ScriptHash

        ms_addrs1.append(nodes[0].getnewaddress())
        ms_addrs1.append(nodes[1].getnewaddress())
        ms_pubkeys1.append(nodes[0].getaddressinfo(ms_addrs1[0])['pubkey'])
        ms_pubkeys1.append(nodes[1].getaddressinfo(ms_addrs1[1])['pubkey'])

        ms_addr1 = nodes[0].addmultisigaddress_part(1, ms_pubkeys1, '', False, True)  # CScriptID256

        coldstakingaddr = nodes[0].validateaddress(nodes[0].getnewaddress(), True)['stakeonly_address']
        for ms_addr in (ms_addr0['address'], ms_addr1['address']):
            changeaddress = {'coldstakingaddress': coldstakingaddr, 'address_standard': ms_addr}
            ro = nodes[0].walletsettings('changeaddress', changeaddress)
            assert (ro['changeaddress']['coldstakingaddress'] == coldstakingaddr)
            assert (ro['changeaddress']['address_standard'] == ms_addr)

            addr_to = nodes[0].getnewaddress()
            rtx = nodes[0].createrawtransaction([], {addr_to: 0.0001})
            ftx = nodes[0].fundrawtransaction(rtx)
            dtx = nodes[0].decoderawtransaction(ftx['hex'])
            n_change = 1 if dtx['vout'][0]['scriptPubKey']['address'] == addr_to else 0
            assert (dtx['vout'][n_change]['scriptPubKey']['address'] == ms_addr)
            stake_addr = dtx['vout'][n_change]['scriptPubKey']['stakeaddress']
            stake_addr_alt = nodes[0].validateaddress(stake_addr, True)['stakeonly_address']
            assert(stake_addr_alt == coldstakingaddr)

        self.log.info('Test sendtypeto with addrstake')
        addrSpend = nodes[0].getnewaddress('addrSpend', 'false', 'false', 'true')
        toScript = nodes[0].buildscript({'recipe': 'ifcoinstake', 'addrstake': coldstakingaddr, 'addrspend': addrSpend})
        rv = nodes[0].sendtypeto('part', 'part', [{'address': addrSpend, 'amount': 1, 'stakeaddress': coldstakingaddr}], '', '', 5, 1, True, {'show_hex': True, 'test_mempool_accept': True})
        tx = nodes[0].decoderawtransaction(rv['hex'])
        found = False
        for output in tx['vout']:
            if output['scriptPubKey']['hex'] == toScript['hex']:
                found = True
                break
        assert(found)

        # coldstakingaddr_ext should be used for the change, spend and stake and destination outputs
        changeaddress = {'coldstakingaddress': coldstakingaddr_ext, 'address_standard': coldstakingaddr_ext}
        ro = nodes[0].walletsettings('changeaddress', changeaddress)
        assert(ro['changeaddress']['coldstakingaddress'] == coldstakingaddr_ext)
        assert(ro['changeaddress']['address_standard'] == coldstakingaddr_ext)

        rv = nodes[0].extkey('key', coldstakingaddr_ext)
        num_derives_before = int(rv['num_derives'])

        addrs_count = {}
        expect_address = nodes[0].deriverangekeys(num_derives_before, num_derives_before + 2, coldstakingaddr_ext)
        addrs_count[expect_address[0]] = 0
        addrs_count[expect_address[2]] = 0
        expect_address_256 = nodes[0].deriverangekeys(num_derives_before + 1, num_derives_before + 1, coldstakingaddr_ext, False, False, False, True)
        addrs_count[expect_address_256[0]] = 0

        rv = nodes[0].sendtypeto('part', 'part', [{'address': addrSpend, 'amount': 1, 'stakeaddress': coldstakingaddr_ext}], '', '', 5, 1, False, {'show_hex': True, 'test_mempool_accept': True})
        tx = nodes[0].decoderawtransaction(rv['hex'])
        for output in tx['vout']:
            for a in addrs_count.keys():
                if output['scriptPubKey']['addresses'][0] == a:
                    addrs_count[a] += 1
                if output['scriptPubKey']['stakeaddresses'][0] == a:
                    addrs_count[a] += 1
        for k, v in addrs_count.items():
            assert(v == 1)

        rv = nodes[0].extkey('key', coldstakingaddr_ext)
        num_derives_after = int(rv['num_derives'])
        assert(num_derives_after == num_derives_before + 3)

        nodes[0].extkey('options', coldstakingaddr_ext, 'num_derives', '111')
        nodes[0].extkey('options', coldstakingaddr_ext, 'num_derives_hardened', '112')
        rv = nodes[0].extkey('key', coldstakingaddr_ext)
        assert(int(rv['num_derives']) == 111)
        assert(int(rv['num_derives_hardened']) == 112)


if __name__ == '__main__':
    ColdStakingTest().main()
