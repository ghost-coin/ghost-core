#!/usr/bin/env python3
<<<<<<< HEAD
# Copyright (c) 2017-2021 The Particl Core developers
=======
# Copyright (c) 2017-2023 The Particl Core developers
>>>>>>> particl/25.x
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_particl import GhostTestFramework, isclose
from test_framework.authproxy import JSONRPCException
from test_framework.script import (
    CScript,
    OP_RETURN,
)
import decimal


class PosTest(GhostTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 4
        self.extra_args = [ ['-debug','-noacceptnonstdtxn','-reservebalance=10000000'] for i in range(self.num_nodes)]
        self.extra_args[0].append('-txindex')  # for getrawtransaction

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self, split=False):
        self.add_nodes(self.num_nodes, extra_args=self.extra_args)
        self.start_nodes()

        self.connect_nodes_bi(0, 1)
        self.connect_nodes_bi(0, 2)
        self.connect_nodes_bi(0, 3)
        self.sync_all()

    def run_test(self):
        nodes = self.nodes

        self.import_genesis_coins_a(nodes[0])

        nodes[1].extkeyimportmaster(nodes[1].mnemonic('new')['master'])
        nodes[2].extkeyimportmaster('sección grito médula hecho pauta posada nueve ebrio bruto buceo baúl mitad')


        addrTo256 = nodes[2].getnewaddress('256 test', 'True', 'False', 'True')
        assert (addrTo256 == 'tpl16a6gjrpfwkqrf8fveajkek07l6a0pxgaayk4y6gyq9zlkxxk2hqqmld6tr')
        [nodes[0].sendtoaddress(addrTo256, 1000) for i in range(4)]

        self.log.info('Test reserve balance')
        nodes[0].walletsettings('stakelimit', {'height': 1})
        assert (isclose(nodes[0].getwalletinfo()['reserve'], 10000000.0))

        ro = nodes[0].reservebalance(True, 100)
        assert (ro['reserve'] == True)
        assert (isclose(ro['amount'], 100.0))

        assert (nodes[0].getwalletinfo()['reserve'] == 100)

        ro = nodes[0].reservebalance(False)
        assert (ro['reserve'] == False)
        assert (ro['amount'] == 0)

        assert (nodes[0].getwalletinfo()['reserve'] == 0)

        assert (self.wait_for_height(nodes[0], 1))
        nodes[0].reservebalance(True, 10000000)

        addrTo = nodes[1].getnewaddress()
        txnHash = nodes[0].sendtoaddress(addrTo, 10)
        assert (nodes[0].getmempoolentry(txnHash)['height'] == 1)

        ro = nodes[0].listtransactions()
        fPass = False
        for txl in ro:
            if txl['address'] == addrTo and txl['amount'] == -10 and txl['category'] == 'send':
                fPass = True
                break
        assert (fPass), 'node0, listtransactions failed.'


        assert (self.wait_for_mempool(nodes[1], txnHash))

        ro = nodes[1].listtransactions()
        assert (len(ro) == 1)
        assert (ro[0]['address'] == addrTo)
        assert (ro[0]['amount'] == 10)
        assert (ro[0]['category'] == 'receive')

        pos_difficulty1 = nodes[0].getposdifficulty()
        self.stakeBlocks(1)
        pos_difficulty = nodes[0].getposdifficulty(1)
<<<<<<< HEAD
        assert(pos_difficulty == pos_difficulty1)
=======
        assert (pos_difficulty == pos_difficulty1)
>>>>>>> particl/25.x

        block2_hash = nodes[0].getblockhash(2)
        ro = nodes[0].getblock(block2_hash)
        assert (txnHash in ro['tx'])


        addrReward = nodes[0].getnewaddress()
        ro = nodes[0].walletsettings('stakingoptions', {'rewardaddress': addrReward})
        assert (ro['stakingoptions']['rewardaddress'] == addrReward)

        addrReward_stakeonly = nodes[0].validateaddress(addrReward, True)['stakeonly_address']
        try:
            ro = nodes[0].walletsettings('stakingoptions', {'rewardaddress': addrReward_stakeonly})
            raise AssertionError('Should have failed.')
        except JSONRPCException as e:
            assert ('Invalid rewardaddress' in e.error['message'])

        self.stakeBlocks(1)
        block3_hash = nodes[0].getblockhash(3)
        coinstakehash = nodes[0].getblock(block3_hash)['tx'][0]
        ro = nodes[0].getrawtransaction(coinstakehash, True)

        fFound = False
        for vout in ro['vout']:
            try:
                addr0 = vout['scriptPubKey']['address']
            except Exception:
                continue
            if addr0 == addrReward:
                fFound = True
<<<<<<< HEAD
                assert(vout['valueSat'] == 600000000)
=======
                assert (vout['valueSat'] == 39637)
>>>>>>> particl/25.x
                break
        assert (fFound)

        self.log.info('Test staking pkh256 outputs')
        nodes[2].walletsettings('stakelimit', {'height': 1})
        nodes[2].reservebalance(False)
        assert (nodes[2].getstakinginfo()['weight'] == 400000000000)

        self.stakeBlocks(1, nStakeNode=2)

        self.log.info('Test rewardaddress')
        addrRewardExt = nodes[0].getnewextaddress()
        ro = nodes[0].walletsettings('stakingoptions', {'rewardaddress': addrRewardExt})
        assert (ro['stakingoptions']['rewardaddress'] == addrRewardExt)
        self.stakeBlocks(1)
        block5_hash = nodes[0].getblockhash(5)
        coinstakehash = nodes[0].getblock(block5_hash)['tx'][0]
        ro = nodes[0].getrawtransaction(coinstakehash, True)

        fFound = False
        for vout in ro['vout']:
            try:
                addr0 = vout['scriptPubKey']['address']
                ro = nodes[0].getaddressinfo(addr0)
                if ro['from_ext_address_id'] == 'xXZRLYvJgbJyrqJhgNzMjEvVGViCdGmVAt':
                    assert (addr0 == 'pgaKYsNmHTuQB83FguN44WW4ADKmwJwV7e')
                    fFound = True
                    assert (vout['valueSat'] == 39637)
            except Exception:
                continue
        assert (fFound)


        addrRewardSx = nodes[0].getnewstealthaddress()
        ro = nodes[0].walletsettings('stakingoptions', {'rewardaddress': addrRewardSx})
        assert (ro['stakingoptions']['rewardaddress'] == addrRewardSx)
        self.stakeBlocks(1)
        block6_hash = nodes[0].getblockhash(6)
        coinstakehash = nodes[0].getblock(block6_hash)['tx'][0]
        ro = nodes[0].getrawtransaction(coinstakehash, True)

        fFound = False
        for vout in ro['vout']:
            try:
                addr0 = vout['scriptPubKey']['address']
                ro = nodes[0].getaddressinfo(addr0)
                if ro['from_stealth_address'] == addrRewardSx:
                    fFound = True
                    assert (vout['valueSat'] == 39637)
            except Exception:
                continue
        assert (fFound)

        self.log.info('Test that unspendable outputs reduce moneysupply')
        header_before = nodes[0].getblockheader(nodes[0].getbestblockhash())
        burn_script = CScript([OP_RETURN, ])
        nodes[0].sendtypeto('part', 'part', [{'address': 'script', 'amount': 100, 'script': burn_script.hex()},])
        self.stakeBlocks(1)
        stakereward = nodes[0].getblockreward(header_before['height'] + 1)['stakereward']
        header_after = nodes[0].getblockheader(nodes[0].getbestblockhash())
        assert(abs(header_before['moneysupply'] - (header_after['moneysupply'] + decimal.Decimal(100.0) - stakereward)) < 0.00000002)

        self.log.info('Test clearing rewardaddress')
        # Set minstakeablevalue above 1.0
        nodes[0].walletsettings('stakingoptions', {'minstakeablevalue': 2.0})
        stake_info = nodes[0].getstakinginfo()
<<<<<<< HEAD
        assert(stake_info['minstakeablevalue'] == 2.0)
=======
        assert (stake_info['minstakeablevalue'] == 2.0)
>>>>>>> particl/25.x

        self.stakeBlocks(1)
        coinstakehash = nodes[0].getblock(nodes[0].getbestblockhash())['tx'][0]
        ro = nodes[0].getrawtransaction(coinstakehash, True)
        stake_kernel_txid = ro['vin'][0]['txid']
        stake_kernel_n = ro['vin'][0]['vout']

        for output in ro['vout'][1:]:
            assert (output['type'] == 'standard')
            assert (output['value'] > 1.0)

        unspent = nodes[0].listunspent()
        for output in unspent:
            assert (output['txid'] != stake_kernel_txid or output['vout'] != stake_kernel_n)

        self.log.info('Test that orphaned coinstake inputs are abandoned')
        nodes[0].rewindchain(6)
        txns = nodes[0].filtertransactions()

        found_orphaned_coinstake = False
        for tx in txns:
            if tx['txid'] == coinstakehash:
                found_orphaned_coinstake = True
                assert (tx['category'] == 'orphaned_stake')
                assert (tx['abandoned'] == True)
        assert (found_orphaned_coinstake)

        unspent = nodes[0].listunspent()
        found_stake_kernel = False
        for output in unspent:
            if output['txid'] == stake_kernel_txid and output['vout'] == stake_kernel_n:
                found_stake_kernel = True
                break
        assert (found_stake_kernel)

        self.log.info('Test pruneorphanedblocks and reindex')
        rv = nodes[0].pruneorphanedblocks()
        assert (rv['files'][0]['blocks_removed'] == 1)
        rv = nodes[0].pruneorphanedblocks(False)
        assert ('Node is shutting down' in rv['note'])
        self.wait_for_node_exit(0, timeout=10)
        self.start_node(0, self.extra_args[0] + ['-wallet=default_wallet',])
        self.log.info('Test pruneorphanedblocks')
        rv = nodes[0].pruneorphanedblocks()
        assert (rv['files'][0]['blocks_removed'] == 0)
        self.connect_nodes_bi(0, 1)
        self.connect_nodes_bi(0, 2)
        self.connect_nodes_bi(0, 3)

        self.log.info('Test that unspendable outputs reduce moneysupply')
        header_before = nodes[0].getblockheader(nodes[0].getbestblockhash())
        burn_script = CScript([OP_RETURN, ])
        nodes[0].sendtypeto('part', 'part', [{'address': 'script', 'amount': 100, 'script': burn_script.hex()},])
        self.stakeBlocks(1)
        stakereward = nodes[0].getblockreward(8)['stakereward']
        header_after = nodes[0].getblockheader(nodes[0].getbestblockhash())
        assert (abs(header_before['moneysupply'] - (header_after['moneysupply'] + decimal.Decimal(100.0) - stakereward)) < 0.00000002 )

        self.log.info('Test that getcoldstakinginfo coin_in_stakeable_script == currently_staking + pending_depth')
        cs_info = nodes[0].getcoldstakinginfo()
        assert (cs_info['pending_depth'] > 0.0)
        assert (cs_info['coin_in_stakeable_script'] == cs_info['currently_staking'] + cs_info['pending_depth'])

        unspent = nodes[0].listunspent()
        for output in unspent:
            assert(output['txid'] != stake_kernel_txid or output['vout'] != stake_kernel_n)

        self.log.info('Test that orphaned coinstake inputs are abandoned')
        nodes[0].rewindchain(nodes[0].getblockcount() - 1)
        txns = nodes[0].filtertransactions()

        found_orphaned_coinstake = False
        for tx in txns:
            if tx['txid'] == coinstakehash:
                found_orphaned_coinstake = True
                assert(tx['category'] == 'orphaned_stake')
                assert(tx['abandoned'] == True)
        assert(found_orphaned_coinstake)

        unspent = nodes[0].listunspent()
        found_stake_kernel = False
        for output in unspent:
            if output['txid'] == stake_kernel_txid and output['vout'] == stake_kernel_n:
                found_stake_kernel = True
                break
        assert(found_stake_kernel)

        self.log.info('Test that getcoldstakinginfo coin_in_stakeable_script == currently_staking + pending_depth')
        cs_info = nodes[0].getcoldstakinginfo()
        assert(cs_info['pending_depth'] > 0.0)
        assert(cs_info['coin_in_stakeable_script'] == cs_info['currently_staking'] + cs_info['pending_depth'])


if __name__ == '__main__':
    PosTest().main()
